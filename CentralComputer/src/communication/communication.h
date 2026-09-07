/*
 * communication.h - Central Computer Communication module (LNC-facing)
 *
 * Talks to the LNC over whichever Transport it's handed (transport.h) --
 * never touches SerialPort, termios, or a TCP socket by name itself.
 * That's deliberate: the project's transport rule says UART vs Ethernet
 * must be a config detail this module doesn't know about. Which
 * concrete Transport is in use (SerialTransport for UART, a TCP-based
 * one for the Ethernet-simulation gateway) is decided once, by whoever
 * constructs it -- see CLAUDE.md's "Ethernet-simulation gateway" note.
 * Uses the shared TLV protocol (Shared/ProtocolTLV/tlv.h) for both
 * directions: tlv_encode() to send, the streaming receiver
 * (tlv_receiver_t + tlv_receiver_feed()) to decode incoming bytes.
 *
 * RAII: the constructor starts the RX thread, the destructor stops it.
 * No separate create()/destroy() to remember to call. Unlike before,
 * this class no longer opens anything itself -- the Transport it's
 * given must already be open and ready to use, and must outlive this
 * Communication object (this class only holds a reference to it).
 *
 * Flow:
 *   send()     : caller thread -> tlv_encode() -> Transport::write()
 *   receive    : rx_thread_ -> Transport::read() -> tlv_receiver_feed()
 *                -> frame_trampoline() -> route_frame() -> on_management_
 *                or on_log_ (whichever was registered via the setters)
 */
#ifndef COMMUNICATION_H
#define COMMUNICATION_H

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include "transport.h"
#include "tlv.h"

class Communication
{
public:
    /* Any callable matching void(const tlv_frame_t &) -- a plain
     * function, or (the common case) a lambda that captures state,
     * e.g. a reference to whichever module should receive the frame. */
    using FrameHandler = std::function<void(const tlv_frame_t &)>;

    /* Takes an already-constructed, already-open Transport (SerialTransport
     * for UART, or a TCP-based one for Ethernet) and starts the RX thread.
     * `transport` must outlive this Communication object -- ownership
     * stays with the caller. */
    explicit Communication(Transport &transport);

    /* Stops the RX thread (joins it) and closes the port. Never throws. */
    ~Communication();

    /* Owns a live thread + file descriptor -- copying or moving would
     * leave two objects both believing they own the same resources. */
    Communication(const Communication &) = delete;
    Communication &operator=(const Communication &) = delete;
    Communication(Communication &&) = delete;
    Communication &operator=(Communication &&) = delete;

    /* Sends one TLV frame to the LNC. Thread-safe: safe to call from
     * more than one thread at once. Throws std::system_error (via
     * Transport::write) on a real transport failure, or
     * std::invalid_argument if value/value_len are contradictory. */
    void send(uint8_t tag, const uint8_t *value, uint8_t value_len);

    /* Register where decoded frames get routed. Until called, both
     * default to a no-op -- safe to receive frames before either
     * module exists yet. Taken by value (not by reference): the usual
     * call site hands in a freshly-written lambda, so std::move inside
     * the setter stores it with no real copy made. */
    void set_management_handler(FrameHandler handler);
    void set_log_handler(FrameHandler handler);

private:
    /* --- transport + protocol state --- */
    Transport &transport_;
    tlv_receiver_t rx_recv_{};

    /* --- TX side: send() may be called from more than one thread --- */
    std::mutex send_mutex_;

    /* --- RX side: where a decoded frame goes, by tag (route_frame) --- */
    FrameHandler on_management_ = [](const tlv_frame_t &) {};
    FrameHandler on_log_ = [](const tlv_frame_t &) {};

    /* --- RX side: the background thread driving all of the above --- */
    std::atomic<bool> running_{true};
    std::thread rx_thread_;

    /* Runs on rx_thread_: reads a chunk from transport_, feeds it to the
     * TLV streaming decoder, dispatches each finished frame via
     * route_frame(). Reconnects (mirroring sermon.cpp) if a read fails. */
    void rx_loop();

    /* Dispatches one decoded frame to on_management_/on_log_ by tag. */
    void route_frame(const tlv_frame_t &frame);

    /* tlv_receiver_feed() needs a plain C function pointer, which a
     * non-static method can never be (it secretly needs a `this`).
     * This static trampoline is a valid C callback; `ctx` carries the
     * real Communication* across that boundary, recovered here and
     * forwarded into route_frame(). */
    static void frame_trampoline(const tlv_frame_t *frame, void *ctx);
};

#endif /* COMMUNICATION_H */