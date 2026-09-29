#include "code_manager.h"
#include "hc12_sender.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <cctype>

enum class GameState {
    Setup,
    Active,
    Paused,
    Defused,
    Exploded
};

CodeManager codeManager;
HC12Sender radio;

GameState gameState = GameState::Setup;
std::mutex gameMutex;

int gameDurationSeconds = 20 * 60;
int frozenGameSeconds = 20 * 60;
int frozenCodeSeconds = 45;

std::chrono::steady_clock::time_point gameStartTime;

std::string stateToString(GameState state) {
    switch (state) {
        case GameState::Setup:
            return "setup";
        case GameState::Active:
            return "active";
        case GameState::Paused:
            return "paused";
        case GameState::Defused:
            return "defused";
        case GameState::Exploded:
            return "exploded";
    }

    return "unknown";
}

int clampInt(int value, int minValue, int maxValue) {
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

int getGameSecondsLeftUnlocked() {
    if (gameState == GameState::Setup) {
        return gameDurationSeconds;
    }

    if (gameState != GameState::Active) {
        return frozenGameSeconds;
    }

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - gameStartTime).count();

    int left = gameDurationSeconds - static_cast<int>(elapsed);

    if (left < 0) {
        left = 0;
    }

    return left;
}

std::string readFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);

    if (!file) {
        return "";
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

bool endsWith(const std::string& text, const std::string& suffix) {
    if (suffix.size() > text.size()) {
        return false;
    }

    return text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string getMimeType(const std::string& path) {
    if (endsWith(path, ".html")) return "text/html; charset=utf-8";
    if (endsWith(path, ".css")) return "text/css; charset=utf-8";
    if (endsWith(path, ".js")) return "application/javascript; charset=utf-8";
    if (endsWith(path, ".png")) return "image/png";
    if (endsWith(path, ".jpg")) return "image/jpeg";
    if (endsWith(path, ".jpeg")) return "image/jpeg";

    return "text/plain; charset=utf-8";
}

void sendHttpResponse(
    int client,
    int statusCode,
    const std::string& statusText,
    const std::string& contentType,
    const std::string& body
) {
    std::stringstream response;

    response << "HTTP/1.1 " << statusCode << " " << statusText << "\r\n";
    response << "Content-Type: " << contentType << "\r\n";
    response << "Content-Length: " << body.size() << "\r\n";
    response << "Connection: close\r\n";
    response << "\r\n";
    response << body;

    std::string data = response.str();
    send(client, data.c_str(), data.size(), 0);
}

std::string receiveRequest(int client) {
    std::string request;
    char buffer[4096];

    while (true) {
        ssize_t bytes = recv(client, buffer, sizeof(buffer), 0);

        if (bytes <= 0) {
            break;
        }

        request.append(buffer, bytes);

        std::size_t headerEnd = request.find("\r\n\r\n");

        if (headerEnd != std::string::npos) {
            std::string headers = request.substr(0, headerEnd);
            std::size_t contentLengthPos = headers.find("Content-Length:");

            if (contentLengthPos == std::string::npos) {
                break;
            }

            std::size_t lineEnd = headers.find("\r\n", contentLengthPos);

            std::string value = headers.substr(
                contentLengthPos + 15,
                lineEnd - contentLengthPos - 15
            );

            int contentLength = std::stoi(value);
            std::size_t totalExpected = headerEnd + 4 + contentLength;

            if (request.size() >= totalExpected) {
                break;
            }
        }
    }

    return request;
}

std::string getMethod(const std::string& request) {
    std::size_t end = request.find(' ');

    if (end == std::string::npos) {
        return "";
    }

    return request.substr(0, end);
}

std::string getPath(const std::string& request) {
    std::size_t firstSpace = request.find(' ');

    if (firstSpace == std::string::npos) {
        return "/";
    }

    std::size_t secondSpace = request.find(' ', firstSpace + 1);

    if (secondSpace == std::string::npos) {
        return "/";
    }

    return request.substr(firstSpace + 1, secondSpace - firstSpace - 1);
}

std::string getBody(const std::string& request) {
    std::size_t pos = request.find("\r\n\r\n");

    if (pos == std::string::npos) {
        return "";
    }

    return request.substr(pos + 4);
}

std::string extractCodeFromJson(const std::string& body) {
    std::size_t keyPos = body.find("\"code\"");

    if (keyPos == std::string::npos) {
        return "";
    }

    std::size_t colonPos = body.find(':', keyPos);

    if (colonPos == std::string::npos) {
        return "";
    }

    std::size_t firstQuote = body.find('"', colonPos + 1);

    if (firstQuote == std::string::npos) {
        return "";
    }

    std::size_t secondQuote = body.find('"', firstQuote + 1);

    if (secondQuote == std::string::npos) {
        return "";
    }

    return body.substr(firstQuote + 1, secondQuote - firstQuote - 1);
}

int extractMinutesFromJson(const std::string& body) {
    std::size_t keyPos = body.find("\"minutes\"");

    if (keyPos == std::string::npos) {
        return 20;
    }

    std::size_t colonPos = body.find(':', keyPos);

    if (colonPos == std::string::npos) {
        return 20;
    }

    std::size_t pos = colonPos + 1;

    while (pos < body.size() && std::isspace(static_cast<unsigned char>(body[pos]))) {
        pos++;
    }

    std::string number;

    while (pos < body.size() && std::isdigit(static_cast<unsigned char>(body[pos]))) {
        number += body[pos];
        pos++;
    }

    if (number.empty()) {
        return 20;
    }

    try {
        return std::stoi(number);
    } catch (...) {
        return 20;
    }
}

std::string makeStatusJson() {
    std::lock_guard<std::mutex> lock(gameMutex);

    int gameSeconds = getGameSecondsLeftUnlocked();

    int codeSeconds = 45;

    if (gameState == GameState::Active) {
        codeSeconds = codeManager.getSecondsLeft();
    } else {
        codeSeconds = frozenCodeSeconds;
    }

    std::stringstream json;

    json << "{";
    json << "\"state\":\"" << stateToString(gameState) << "\",";
    json << "\"gameSecondsLeft\":" << gameSeconds << ",";
    json << "\"codeSecondsLeft\":" << codeSeconds << ",";
    json << "\"gameDurationSeconds\":" << gameDurationSeconds;
    json << "}";

    return json.str();
}

void handleStatus(int client) {
    sendHttpResponse(
        client,
        200,
        "OK",
        "application/json; charset=utf-8",
        makeStatusJson()
    );
}

void handleStart(int client, const std::string& body) {
    int minutes = extractMinutesFromJson(body);
    minutes = clampInt(minutes, 1, 180);

    std::string newCode;

    {
        std::lock_guard<std::mutex> lock(gameMutex);

        gameDurationSeconds = minutes * 60;
        frozenGameSeconds = gameDurationSeconds;

        codeManager.reset();
        frozenCodeSeconds = codeManager.getSecondsLeft();

        gameStartTime = std::chrono::steady_clock::now();
        gameState = GameState::Active;

        newCode = codeManager.getCode();
    }

    std::cout << "[BACKEND] Partita avviata. Durata: " << minutes << " minuti" << std::endl;
    std::cout << "[BACKEND] Codice iniziale: " << newCode << std::endl;

    radio.sendCode(newCode);

    std::stringstream json;
    json << "{";
    json << "\"result\":\"started\",";
    json << "\"minutes\":" << minutes;
    json << "}";

    sendHttpResponse(
        client,
        200,
        "OK",
        "application/json; charset=utf-8",
        json.str()
    );
}

void handlePause(int client) {
    {
        std::lock_guard<std::mutex> lock(gameMutex);

        if (gameState == GameState::Active) {
            frozenGameSeconds = getGameSecondsLeftUnlocked();
            frozenCodeSeconds = codeManager.getSecondsLeft();
            gameState = GameState::Paused;

            std::cout << "[BACKEND] Gioco in pausa" << std::endl;
        }
    }

    sendHttpResponse(
        client,
        200,
        "OK",
        "application/json; charset=utf-8",
        "{\"result\":\"paused\"}"
    );
}

void handleResume(int client) {
    std::string newCode;

    {
        std::lock_guard<std::mutex> lock(gameMutex);

        if (gameState == GameState::Paused) {
            gameDurationSeconds = frozenGameSeconds;
            gameStartTime = std::chrono::steady_clock::now();

            codeManager.reset();
            frozenCodeSeconds = codeManager.getSecondsLeft();

            gameState = GameState::Active;
            newCode = codeManager.getCode();

            std::cout << "[BACKEND] Gioco ripreso" << std::endl;
            std::cout << "[BACKEND] Nuovo codice dopo pausa: " << newCode << std::endl;
        }
    }

    if (!newCode.empty()) {
        radio.sendCode(newCode);
    }

    sendHttpResponse(
        client,
        200,
        "OK",
        "application/json; charset=utf-8",
        "{\"result\":\"active\"}"
    );
}

void handleSubmit(int client, const std::string& body) {
    std::string inputCode = extractCodeFromJson(body);
    std::string result;

    {
        std::lock_guard<std::mutex> lock(gameMutex);

        if (gameState != GameState::Active) {
            result = stateToString(gameState);
        } else {
            int gameLeft = getGameSecondsLeftUnlocked();

            if (gameLeft <= 0) {
                gameState = GameState::Exploded;
                frozenGameSeconds = 0;
                frozenCodeSeconds = codeManager.getSecondsLeft();
                result = "exploded";
            } else {
                frozenGameSeconds = gameLeft;
                frozenCodeSeconds = codeManager.getSecondsLeft();

                if (codeManager.checkCode(inputCode)) {
                    gameState = GameState::Defused;
                    result = "defused";
                } else {
                    gameState = GameState::Exploded;
                    result = "exploded";
                }
            }
        }
    }

    std::string json = "{\"result\":\"" + result + "\"}";

    sendHttpResponse(
        client,
        200,
        "OK",
        "application/json; charset=utf-8",
        json
    );
}

void handleReset(int client) {
    {
        std::lock_guard<std::mutex> lock(gameMutex);

        gameState = GameState::Setup;
        gameDurationSeconds = 20 * 60;
        frozenGameSeconds = gameDurationSeconds;
        frozenCodeSeconds = 45;

        codeManager.reset();
    }

    sendHttpResponse(
        client,
        200,
        "OK",
        "application/json; charset=utf-8",
        "{\"result\":\"setup\"}"
    );
}

void serveStaticFile(int client, const std::string& path) {
    std::string filePath;

    if (path == "/") {
        filePath = "html/index.html";
    } else {
        filePath = "html" + path;
    }

    std::string body = readFile(filePath);

    if (body.empty()) {
        sendHttpResponse(
            client,
            404,
            "Not Found",
            "text/plain; charset=utf-8",
            "File non trovato"
        );
        return;
    }

    sendHttpResponse(
        client,
        200,
        "OK",
        getMimeType(filePath),
        body
    );
}

void handleClient(int client) {
    std::string request = receiveRequest(client);

    if (request.empty()) {
        close(client);
        return;
    }

    std::string method = getMethod(request);
    std::string path = getPath(request);
    std::string body = getBody(request);

    if (method == "GET" && path == "/api/status") {
        handleStatus(client);
    } else if (method == "POST" && path == "/api/start") {
        handleStart(client, body);
    } else if (method == "POST" && path == "/api/pause") {
        handlePause(client);
    } else if (method == "POST" && path == "/api/resume") {
        handleResume(client);
    } else if (method == "POST" && path == "/api/submit") {
        handleSubmit(client, body);
    } else if (method == "POST" && path == "/api/reset") {
        handleReset(client);
    } else if (method == "GET") {
        serveStaticFile(client, path);
    } else {
        sendHttpResponse(
            client,
            405,
            "Method Not Allowed",
            "text/plain; charset=utf-8",
            "Metodo non consentito"
        );
    }

    close(client);
}

void codeRotationThread() {
    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));

        std::string newCode;
        bool changed = false;
        bool timeExpired = false;

        {
            std::lock_guard<std::mutex> lock(gameMutex);

            if (gameState == GameState::Active) {
                int gameLeft = getGameSecondsLeftUnlocked();

                if (gameLeft <= 0) {
                    gameState = GameState::Exploded;
                    frozenGameSeconds = 0;
                    frozenCodeSeconds = codeManager.getSecondsLeft();
                    timeExpired = true;
                } else {
                    changed = codeManager.update();

                    if (changed) {
                        newCode = codeManager.getCode();
                    }
                }
            }
        }

        if (changed) {
            std::cout << "[BACKEND] Nuovo codice generato: " << newCode << std::endl;
            radio.sendCode(newCode);
        }

        if (timeExpired) {
            std::cout << "[BACKEND] Tempo partita scaduto: bomba esplosa" << std::endl;
        }
    }
}

int main() {
    const int PORT = 8080;

    radio.begin("/dev/ttyUSB0");

    std::thread rotator(codeRotationThread);
    rotator.detach();

    int serverFd = socket(AF_INET, SOCK_STREAM, 0);

    if (serverFd < 0) {
        std::cerr << "Errore creazione socket" << std::endl;
        return 1;
    }

    int opt = 1;
    setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(serverFd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        std::cerr << "Errore bind porta " << PORT << std::endl;
        close(serverFd);
        return 1;
    }

    if (listen(serverFd, 10) < 0) {
        std::cerr << "Errore listen" << std::endl;
        close(serverFd);
        return 1;
    }

    std::cout << "[BACKEND] DuckBomb pronto in schermata setup" << std::endl;
    std::cout << "[BACKEND] Apri dal browser: http://IP_DELLA_UNO_Q:" << PORT << std::endl;

    while (true) {
        int client = accept(serverFd, nullptr, nullptr);

        if (client < 0) {
            continue;
        }

        std::thread clientThread(handleClient, client);
        clientThread.detach();
    }

    close(serverFd);
    return 0;
}