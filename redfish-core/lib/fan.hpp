#pragma once

#include "app.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "utils/chassis_utils.hpp"

#include <memory>
#include <optional>
#include <string>

namespace redfish
{

inline void updateFanList(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          const std::string& chassisId,
                          const std::string& fanPath)
{
    std::string fanName = sdbusplus::message::object_path(fanPath).filename();
    if (fanName.empty())
    {
        return;
    }

    nlohmann::json item;
    item["@odata.id"] =
        crow::utility::urlFromPieces("redfish", "v1", "Chassis", chassisId,
                                     "ThermalSubsystem", "Fans", fanName);

    nlohmann::json& fanList = asyncResp->res.jsonValue["Members"];
    fanList.emplace_back(std::move(item));
    asyncResp->res.jsonValue["Members@odata.count"] = fanList.size();
}

inline bool checkFanId(const std::string& fanPath, const std::string& fanId)
{
    std::string fanName = sdbusplus::message::object_path(fanPath).filename();

    return !(fanName.empty() || fanName != fanId);
}

inline void fanCollection(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          const std::string& fanPath,
                          const std::string& chassisId)
{
    dbus::utility::getAssociationEndPoints(
        fanPath + "/sensors",
        [asyncResp, fanPath,
         chassisId](const boost::system::error_code& ec,
                    const dbus::utility::MapperEndPoints& endpoints) {
        if (ec)
        {
            if (ec.value() != EBADR)
            {
                messages::internalError(asyncResp->res);
                return;
            }
        }

        if (endpoints.empty())
        {
            updateFanList(asyncResp, chassisId, fanPath);
            return;
        }

        for (const auto& endpoint : endpoints)
        {
            updateFanList(asyncResp, chassisId, endpoint);
        }
        });
}

inline void doFanCollection(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& chassisId,
                            const std::optional<std::string>& validChassisPath)
{
    if (!validChassisPath)
    {
        messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
        return;
    }

    asyncResp->res.addHeader(
        boost::beast::http::field::link,
        "</redfish/v1/JsonSchemas/FanCollection/FanCollection.json>; rel=describedby");
    asyncResp->res.jsonValue["@odata.type"] = "#FanCollection.FanCollection";
    asyncResp->res.jsonValue["@odata.id"] = crow::utility::urlFromPieces(
        "redfish", "v1", "Chassis", chassisId, "ThermalSubsystem", "Fans");
    asyncResp->res.jsonValue["Name"] = "Fan Collection";
    asyncResp->res.jsonValue["Description"] =
        "The collection of Fan resource instances " + chassisId;
    asyncResp->res.jsonValue["Members"] = nlohmann::json::array();
    asyncResp->res.jsonValue["Members@odata.count"] = 0;

    dbus::utility::getAssociationEndPoints(
        *validChassisPath + "/cooled_by",
        [asyncResp,
         chassisId](const boost::system::error_code& ec,
                    const dbus::utility::MapperEndPoints& endpoints) {
        if (ec)
        {
            if (ec.value() != EBADR)
            {
                messages::internalError(asyncResp->res);
            }
            return;
        }

        for (const auto& endpoint : endpoints)
        {
            fanCollection(asyncResp, endpoint, chassisId);
        }
        });
}

inline void
    handleFanCollectionHead(App& app, const crow::Request& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& chassisId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    redfish::chassis_utils::getValidChassisPath(
        asyncResp, chassisId,
        [asyncResp,
         chassisId](const std::optional<std::string>& validChassisPath) {
        if (!validChassisPath)
        {
            messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
            return;
        }
        asyncResp->res.addHeader(
            boost::beast::http::field::link,
            "</redfish/v1/JsonSchemas/FanCollection/FanCollection.json>; rel=describedby");
        });
}

inline void
    handleFanCollectionGet(App& app, const crow::Request& req,
                           const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           const std::string& chassisId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    redfish::chassis_utils::getValidChassisPath(
        asyncResp, chassisId,
        std::bind_front(doFanCollection, asyncResp, chassisId));
}

inline void requestRoutesFanCollection(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/ThermalSubsystem/Fans/")
        .privileges(redfish::privileges::headFanCollection)
        .methods(boost::beast::http::verb::head)(
            std::bind_front(handleFanCollectionHead, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/ThermalSubsystem/Fans/")
        .privileges(redfish::privileges::getFanCollection)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleFanCollectionGet, std::ref(app)));
}

inline void getValidFanPath(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& validChassisPath, const std::string& fanId,
    std::function<void(const std::string& fanPath,
                       const std::string& fanSensorPath)>&& callback)
{
    dbus::utility::getAssociationEndPoints(
        validChassisPath + "/cooled_by",
        [asyncResp, fanId, callback{std::move(callback)}](
            const boost::system::error_code& ec,
            const dbus::utility::MapperEndPoints& ivEndpoints) {
        if (ec)
        {
            if (ec.value() != EBADR)
            {
                messages::internalError(asyncResp->res);
            }
            messages::resourceNotFound(asyncResp->res, "Fan", fanId);
            return;
        }

        messages::resourceNotFound(asyncResp->res, "Fan", fanId);
        for (const auto& ivEndpoint : ivEndpoints)
        {
            dbus::utility::getAssociationEndPoints(
                ivEndpoint + "/sensors",
                [asyncResp, fanId, ivEndpoint, callback](
                    const boost::system::error_code& ec1,
                    const dbus::utility::MapperEndPoints& sensorEndpoints) {
                if (ec1 && ec1.value() != EBADR)
                {
                    messages::internalError(asyncResp->res);
                    return;
                }

                if (sensorEndpoints.empty())
                {
                    if (checkFanId(ivEndpoint, fanId))
                    {
                        asyncResp->res.clear();
                        callback(ivEndpoint, "");
                    }
                    return;
                }

                for (const auto& sensorEndpoint : sensorEndpoints)
                {
                    if (checkFanId(sensorEndpoint, fanId))
                    {
                        asyncResp->res.clear();
                        callback(ivEndpoint, sensorEndpoint);
                        return;
                    }
                }
                });
        }
        });
}

inline void getFanHealth(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                         const std::string& service, const std::string& path)
{
    sdbusplus::asio::getProperty<bool>(
        *crow::connections::systemBus, service, path,
        "xyz.openbmc_project.State.Decorator.OperationalStatus", "Functional",
        [asyncResp](const boost::system::error_code& ec, bool value) {
        if (ec)
        {
            if (ec.value() != EBADR)
            {
                messages::internalError(asyncResp->res);
            }
            return;
        }

        if (!value)
        {
            asyncResp->res.jsonValue["Status"]["Health"] = "Critical";
        }
        });
}

inline void getFanState(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                        const std::string& service, const std::string& path)
{
    sdbusplus::asio::getProperty<bool>(
        *crow::connections::systemBus, service, path,
        "xyz.openbmc_project.Inventory.Item", "Present",
        [asyncResp](const boost::system::error_code& ec, bool value) {
        if (ec)
        {
            if (ec.value() != EBADR)
            {
                messages::internalError(asyncResp->res);
            }
            return;
        }

        if (!value)
        {
            asyncResp->res.jsonValue["Status"]["State"] = "Absent";
        }
        });
}

inline void getFanAsset(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                        const std::string& service, const std::string& path)
{
    sdbusplus::asio::getAllProperties(
        *crow::connections::systemBus, service, path,
        "xyz.openbmc_project.Inventory.Decorator.Asset",
        [asyncResp](const boost::system::error_code& ec,
                    const dbus::utility::DBusPropertiesMap& propertiesList) {
        if (ec)
        {
            if (ec.value() != EBADR)
            {
                messages::internalError(asyncResp->res);
            }
            return;
        }

        const std::string* partNumber = nullptr;
        const std::string* serialNumber = nullptr;
        const std::string* manufacturer = nullptr;
        const std::string* model = nullptr;

        const bool success = sdbusplus::unpackPropertiesNoThrow(
            dbus_utils::UnpackErrorPrinter(), propertiesList, "PartNumber",
            partNumber, "SerialNumber", serialNumber, "Manufacturer",
            manufacturer, "Model", model);

        if (!success)
        {
            messages::internalError(asyncResp->res);
            return;
        }

        if (partNumber != nullptr)
        {
            asyncResp->res.jsonValue["PartNumber"] = *partNumber;
        }

        if (serialNumber != nullptr)
        {
            asyncResp->res.jsonValue["SerialNumber"] = *serialNumber;
        }

        if (manufacturer != nullptr)
        {
            asyncResp->res.jsonValue["Manufacturer"] = *manufacturer;
        }

        if (model != nullptr)
        {
            asyncResp->res.jsonValue["Model"] = *model;
        }
        });
}

inline void getFanLocation(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           const std::string& service, const std::string& path)
{
    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, service, path,
        "xyz.openbmc_project.Inventory.Decorator.LocationCode", "LocationCode",
        [asyncResp](const boost::system::error_code& ec,
                    const std::string& value) {
        if (ec)
        {
            if (ec.value() != EBADR)
            {
                messages::internalError(asyncResp->res);
            }
            return;
        }

        asyncResp->res.jsonValue["Location"]["PartLocation"]["ServiceLabel"] =
            value;
        });
}

inline void
    getFanSpeedPercent(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                       const std::string& service, const std::string& path,
                       const std::string& chassisId, const std::string& fanId)
{
    sdbusplus::asio::getProperty<double>(
        *crow::connections::systemBus, service, path,
        "xyz.openbmc_project.Sensor.Value", "Value",
        [asyncResp, chassisId, fanId](const boost::system::error_code& ec,
                                      double value) {
        if (ec)
        {
            if (ec.value() != EBADR)
            {
                messages::internalError(asyncResp->res);
            }
            return;
        }

        asyncResp->res.jsonValue["SpeedPercent"]["Reading"] = value;
        asyncResp->res.jsonValue["SpeedPercent"]["DataSourceUri"] =
            crow::utility::urlFromPieces("redfish", "v1", "Chassis", chassisId,
                                         "Sensors", "Fans", fanId);
        });
}

inline void doFanGet(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                     const std::string& chassisId, const std::string& fanId,
                     const std::optional<std::string>& validChassisPath)
{
    if (!validChassisPath)
    {
        messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
        return;
    }

    getValidFanPath(
        asyncResp, *validChassisPath, fanId,
        [asyncResp, chassisId, fanId](const std::string& fanPath,
                                      const std::string& fanSensorPath) {
        asyncResp->res.addHeader(
            boost::beast::http::field::link,
            "</redfish/v1/JsonSchemas/Fan/Fan.json>; rel=describedby");
        asyncResp->res.jsonValue["@odata.type"] = "#Fan.v1_3_0.Fan";
        asyncResp->res.jsonValue["Name"] = fanId;
        asyncResp->res.jsonValue["Id"] = fanId;
        asyncResp->res.jsonValue["@odata.id"] =
            crow::utility::urlFromPieces("redfish", "v1", "Chassis", chassisId,
                                         "ThermalSubsystem", "Fans", fanId);
        asyncResp->res.jsonValue["Status"]["Health"] = "OK";
        asyncResp->res.jsonValue["Status"]["State"] = "Enabled";

        dbus::utility::getDbusObject(
            fanPath, {},
            [asyncResp, fanPath, fanSensorPath, chassisId,
             fanId](const boost::system::error_code& ec,
                    const dbus::utility::MapperGetObject& object) {
            if (ec || object.empty())
            {
                messages::internalError(asyncResp->res);
                return;
            }

            if (!fanSensorPath.empty())
            {
                getFanHealth(asyncResp, object.begin()->first, fanSensorPath);
                getFanSpeedPercent(asyncResp, object.begin()->first,
                                   fanSensorPath, chassisId, fanId);
            }
            else
            {
                getFanHealth(asyncResp, object.begin()->first, fanPath);
            }

            getFanState(asyncResp, object.begin()->first, fanPath);
            getFanAsset(asyncResp, object.begin()->first, fanPath);
            getFanLocation(asyncResp, object.begin()->first, fanPath);
            });
        });
}

inline void handleFanHead(App& app, const crow::Request& req,
                          const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          const std::string& chassisId,
                          const std::string& fanId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    redfish::chassis_utils::getValidChassisPath(
        asyncResp, chassisId,
        [asyncResp, fanId](const std::optional<std::string>& validChassisPath) {
        if (!validChassisPath)
        {
            messages::resourceNotFound(asyncResp->res, "Fan", fanId);
            return;
        }

        // Get the correct Path and Service that match the input parameters
        getValidFanPath(asyncResp, *validChassisPath, fanId,
                        [asyncResp](const std::string&, const std::string&) {
            asyncResp->res.addHeader(
                boost::beast::http::field::link,
                "</redfish/v1/JsonSchemas/Fan/Fan.json>; rel=describedby");
        });
        });
}

inline void handleFanGet(App& app, const crow::Request& req,
                         const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                         const std::string& chassisId, const std::string& fanId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    redfish::chassis_utils::getValidChassisPath(
        asyncResp, chassisId,
        std::bind_front(doFanGet, asyncResp, chassisId, fanId));
}

inline void requestRoutesFan(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/ThermalSubsystem/Fans/<str>/")
        .privileges(redfish::privileges::headFan)
        .methods(boost::beast::http::verb::head)(
            std::bind_front(handleFanHead, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/ThermalSubsystem/Fans/<str>/")
        .privileges(redfish::privileges::getFan)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleFanGet, std::ref(app)));
}

} // namespace redfish
