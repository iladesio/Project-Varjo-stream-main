// Copyright 2021 Varjo Technologies Oy. All rights reserved.

#pragma once
#include <string>

#include <Varjo.h>

// C++ helper for handling Varjo API session instances
class Session final
{
public:
    // Constructor. Initializes a new session.
    Session();

    // Destructor. Terminates the session.
    ~Session();

    // Checks whether session is valid i.e. it was successfully initialized in constructor
    bool isValid() const;

    // Implicit cast operator for using this class in Varjo API calls
    operator varjo_Session*() const;

    // Gets current error state from session
    std::string getError() const;

    // Gets current time of clock used by Varjo API
    varjo_Nanoseconds getCurrentTime() const;

private:
    varjo_Session* m_sessionPointer = nullptr;
};
