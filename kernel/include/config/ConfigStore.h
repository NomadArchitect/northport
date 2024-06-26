#pragma once

#include <Span.h>

namespace Npk::Config
{
    void InitConfigStore();
    void LateInitConfigStore();
    sl::StringSpan GetConfig(sl::StringSpan key);

    size_t GetConfigNumber(sl::StringSpan key, size_t orDefault);
}
