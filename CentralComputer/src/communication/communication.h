/*
 * communication.h - Central Computer Communication module (LNC-facing)
 *
 * Owns the serial link to one LNC exclusively -- sits on top of
 * SerialPort (uartTransport/serial.h), never touches termios/fd details
 * itself. Uses the shared TLV protocol (Shared/ProtocolTLV/tlv.h) for
 * both directions: tlv_encode() to send, the streaming receiver
 * (tlv_receiver_t + tlv_receiver_feed()) to decode incoming bytes.
 *
 * RAII, matching SerialPort's own style: the constructor opens the port
 * and starts the RX thread, the destructor stops the thread and closes
 * everything down automatically. No separate create()/destroy() to
 * remember to call.
 *
 * Flow:
 *   send()     : caller thread -> tlv_encode() -> SerialPort::write()
 *   receive    : rx_thread_ -> SerialPort::read() -> tlv_receiver_feed()
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

#include "serial.h"
#include "tlv.h"

class Communication
{
public:
    /* Any callable matching void(const tlv_frame_t &) -- a plain
     * function, or (the common case) a lambda that captures state,
     * e.g. a reference to whichever module should receive the frame. */
    using FrameHandler = std::function<void(const tlv_frame_t &)>;

    /* Opens `path` (via SerialPort) and starts the RX thread. Throws
     * std::system_error (propagated from SerialPort) if the port can't
     * be opened/configured. */
    explicit Communication(const std::string &path);

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
     * SerialPort::write) on a real transport failure, or
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
    SerialPort port_;
    tlv_receiver_t rx_recv_{};

    /* --- TX side: send() may be called from more than one thread --- */
    std::mutex send_mutex_;

    /* --- RX side: where a decoded frame goes, by tag (route_frame) --- */
    FrameHandler on_management_ = [](const tlv_frame_t &) {};
    FrameHandler on_log_ = [](const tlv_frame_t &) {};

    /* --- RX side: the background thread driving all of the above --- */
    std::atomic<bool> running_{true};
    std::thread rx_thread_;

    /* Runs on rx_thread_: reads a chunk from port_, feeds it to the TLV
     * streaming decoder, dispatches each finished frame via
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