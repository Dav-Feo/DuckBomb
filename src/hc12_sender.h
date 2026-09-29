#ifndef HC12_SENDER_H
#define HC12_SENDER_H

#include <string>

class HC12Sender {
public:
    HC12Sender();

    bool begin(const std::string& port);
    bool sendCode(const std::string& code);

private:
    bool ready;

    bool sendBridgeNotify(const std::string& method, const std::string& code);
};

#endif