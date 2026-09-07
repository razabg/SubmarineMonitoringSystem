/*
 * serial_transport.h - adapts SerialPort to the Transport interface.
 *
 * SerialPort itself is untouched (already hardware-tested) -- this class
 * just holds one and forwards each Transport call to it. Used when UART
 * is the chosen transport; see transport.h for the interface this
 * satisfies, and CLAUDE.md's "Ethernet-simulation gateway" note for why
 * this exists (so Communication can be handed either this or a TCP-based
 * Transport, chosen once at construction, with no code of its own that
 * knows which).
 */
#ifndef SERIAL_TRANSPORT_H
#define SERIAL_TRANSPORT_H

#include "transport.h"
#include "serial.h"

class SerialTransport : public Transport
{
public:
    explicit SerialTransport(std::string path);

    long read(uint8_t *buf, size_t len) override;
    long write(const uint8_t *buf, size_t len) override;
    void reconnect() override;

private:
    SerialPort port_;
};

#endif /* SERIAL_TRANSPORT_H */
