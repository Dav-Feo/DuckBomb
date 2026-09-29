#ifndef CODE_MANAGER_H
#define CODE_MANAGER_H

#include <chrono>
#include <string>

class CodeManager {
public:
    CodeManager();

    bool update();
    void reset();

    std::string getCode() const;
    int getSecondsLeft() const;
    bool checkCode(const std::string& input) const;

private:
    static constexpr int VALIDITY_SECONDS = 45;

    std::string currentCode;
    std::chrono::steady_clock::time_point lastChange;

    std::string generateCode();
    std::string cleanCode(const std::string& input) const;
};

#endif