#include "hc12_sender.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <sys/time.h>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

static const char* ROUTER_SOCKET = "/var/run/arduino-router.sock";
static const char* MCU_METHOD = "hc12_send_code";

HC12Sender::HC12Sender()
    : ready(false) {
}

bool HC12Sender::begin(const std::string& port) {
    ready = true;

    std::cout << "[HC-12] Modalità UNO Q Bridge attiva" << std::endl;
    std::cout << "[HC-12] Porta legacy ignorata: " << port << std::endl;
    std::cout << "[HC-12] Router: " << ROUTER_SOCKET << std::endl;
    std::cout << "[HC-12] Metodo MCU: " << MCU_METHOD << std::endl;

    return true;
}

static void appendMsgpackString(
    std::vector<uint8_t>& out,
    const std::string& value
) {
    const std::size_t len = value.size();

    if (len <= 31) {
        out.push_back(static_cast<uint8_t>(0xA0 | len));
    } else if (len <= 255) {
        out.push_back(0xD9);
        out.push_back(static_cast<uint8_t>(len));
    } else if (len <= 65535) {
        out.push_back(0xDA);
        out.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>(len & 0xFF));
    } else {
        out.push_back(0xDB);
        out.push_back(static_cast<uint8_t>((len >> 24) & 0xFF));
        out.push_back(static_cast<uint8_t>((len >> 16) & 0xFF));
        out.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>(len & 0xFF));
    }

    out.insert(out.end(), value.begin(), value.end());
}

static bool writeAll(
    int fd,
    const std::vector<uint8_t>& data
) {
    std::size_t sent = 0;

    while (sent < data.size()) {
        ssize_t written = write(
            fd,
            data.data() + sent,
            data.size() - sent
        );

        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }

            std::cerr
                << "[HC-12] Errore write socket: "
                << std::strerror(errno)
                << std::endl;

            return false;
        }

        if (written == 0) {
            return false;
        }

        sent += static_cast<std::size_t>(written);
    }

    return true;
}

bool HC12Sender::sendBridgeNotify(
    const std::string& method,
    const std::string& code
) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);

    if (fd < 0) {
        std::cerr
            << "[HC-12] Impossibile creare socket: "
            << std::strerror(errno)
            << std::endl;

        return false;
    }

    // Evita che il backend resti bloccato all'infinito
    timeval timeout{};
    timeout.tv_sec = 3;
    timeout.tv_usec = 0;

    setsockopt(
        fd,
        SOL_SOCKET,
        SO_RCVTIMEO,
        &timeout,
        sizeof(timeout)
    );

    sockaddr_un address{};
    address.sun_family = AF_UNIX;

    std::strncpy(
        address.sun_path,
        ROUTER_SOCKET,
        sizeof(address.sun_path) - 1
    );

    if (
        connect(
            fd,
            reinterpret_cast<sockaddr*>(&address),
            sizeof(address)
        ) < 0
    ) {
        std::cerr
            << "[HC-12] Impossibile collegarsi al router: "
            << std::strerror(errno)
            << std::endl;

        close(fd);
        return false;
    }

    /*
        MessagePack RPC REQUEST:

        [
            0,
            1,
            "hc12_send_code",
            ["ABC123"]
        ]

        0 = REQUEST
        1 = message id
    */

    std::vector<uint8_t> payload;

    payload.push_back(0x94);  // array 4 elementi
    payload.push_back(0x00);  // REQUEST
    payload.push_back(0x01);  // msg id = 1

    appendMsgpackString(
        payload,
        method
    );

    payload.push_back(0x91);  // array con 1 parametro

    appendMsgpackString(
        payload,
        code
    );

    if (!writeAll(fd, payload)) {
        close(fd);
        return false;
    }

    /*
        Aspettiamo la risposta del Router/MCU.

        Non facciamo parsing completo MessagePack:
        per questo progetto ci basta verificare che
        sia arrivata una risposta RPC.
    */

    uint8_t response[1024];

    ssize_t received = recv(
        fd,
        response,
        sizeof(response),
        0
    );

    close(fd);

    if (received <= 0) {
        std::cerr
            << "[HC-12] Nessuna risposta RPC dalla MCU"
            << std::endl;

        return false;
    }

    /*
        Una risposta MessagePack RPC ha forma:

        [1, msgid, error, result]

        0x94 = array di 4 elementi
        secondo byte = 1 (RESPONSE)
    */

    if (
        received < 2 ||
        response[0] != 0x94 ||
        response[1] != 0x01
    ) {
        std::cerr
            << "[HC-12] Risposta RPC non valida"
            << std::endl;

        return false;
    }

    return true;
}

bool HC12Sender::sendCode(const std::string& code) {
    if (!ready) {
        std::cerr
            << "[HC-12] Sender non inizializzato"
            << std::endl;

        return false;
    }

    if (code.length() != 6) {
        std::cerr
            << "[HC-12] Codice non valido: "
            << code
            << std::endl;

        return false;
    }

    std::cout
        << "[HC-12] Invio codice alla MCU: "
        << code
        << std::endl;

    bool ok = sendBridgeNotify(
        MCU_METHOD,
        code
    );

    if (ok) {
        std::cout << "----------------------------------------" << std::endl;
        std::cout
            << "[HC-12] Codice accettato dalla MCU: "
            << code
            << std::endl;

        std::cout
            << "[HC-12] Messaggio radio: <CODE:"
            << code
            << ">"
            << std::endl;

        std::cout
            << "[HC-12] Stato: RPC COMPLETATA"
            << std::endl;

        std::cout << "----------------------------------------" << std::endl;
    } else {
        std::cerr
            << "[HC-12] ERRORE invio codice: "
            << code
            << std::endl;
    }

    return ok;
}