#include "code_manager.h"

#include <algorithm>
#include <cctype>
#include <random>

CodeManager::CodeManager() {
    reset();
}

void CodeManager::reset() {
    currentCode = generateCode();
    lastChange = std::chrono::steady_clock::now();
}

std::string CodeManager::generateCode() {
    const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dist(0, static_cast<int>(chars.size() - 1));

    std::string code;

    for (int i = 0; i < 6; i++) {
        code += chars[dist(gen)];
    }

    return code;
}

bool CodeManager::update() {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastChange).count();

    if (elapsed >= VALIDITY_SECONDS) {
        currentCode = generateCode();
        lastChange = now;
        return true;
    }

    return false;
}

std::string CodeManager::getCode() const {
    return currentCode;
}

int CodeManager::getSecondsLeft() const {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastChange).count();

    int left = VALIDITY_SECONDS - static_cast<int>(elapsed);

    if (left < 0) {
        left = 0;
    }

    return left;
}

std::string CodeManager::cleanCode(const std::string& input) const {
    std::string cleaned;

    for (char c : input) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            cleaned += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
    }

    return cleaned;
}

bool CodeManager::checkCode(const std::string& input) const {
    return cleanCode(input) == currentCode;
}