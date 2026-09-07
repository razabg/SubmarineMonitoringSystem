/*
 * serial_transport.cpp - adapts SerialPort to the Transport interface.
 * See serial_transport.h for the class-level overview.
 */
#include "serial_transport.h"

#include <utility>

SerialTransport::SerialTransport(std::string path) : port_(std::move(path))
{
}

long SerialTransport::read(uint8_t *buf, size_t len)
{
    return port_.read(buf, len);
}

long SerialTransport::write(const uint8_t *buf, size_t len)
{
    return port_.write(buf, len);
}

void SerialTransport::reconnect()
{
    port_.reconnect();
}
