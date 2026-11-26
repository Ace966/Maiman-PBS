#pragma once

#include "DeviceConfiguration.h"
#include "GuiModel.h"

#include <optional>

class GuiFactory {
public:
    // Pure configuration: validates existence, not register access rights or firmware masks.
    static std::optional<DeviceWidgetDesc> create(
        const QString& filename, const DeviceConfiguration& configuration, QString* error = nullptr);
};
