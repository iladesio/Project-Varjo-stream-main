// Copyright 2021 Varjo Technologies Oy. All rights reserved.

#include "Session.hpp"

Session::Session()
    : m_sessionPointer(varjo_SessionInit())
{
}

Session::~Session()
{
    if (m_sessionPointer) {
        varjo_SessionShutDown(m_sessionPointer);
    }
}

bool Session::isValid() const { return m_sessionPointer != nullptr; }

Session::operator varjo_Session*() const { return m_sessionPointer; }

std::string Session::getError() const
{
    varjo_Error error = varjo_Error_InvalidSession;
    if (isValid()) {
        error = varjo_GetError(m_sessionPointer);
        if (error == varjo_NoError) {
            return {};
        }
    }

    return varjo_GetErrorDesc(error);
}

varjo_Nanoseconds Session::getCurrentTime() const
{
    if (isValid()) {
        return varjo_GetCurrentTime(m_sessionPointer);
    }

    return 0;
}
