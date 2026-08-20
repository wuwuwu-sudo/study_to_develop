#pragma once
#include <string>
#include <iostream>

class SessionId {
public:
    explicit SessionId(std::string value);

    const std::string& value() const;

private:
    std::string value_;
};