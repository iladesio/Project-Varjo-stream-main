// Copyright 2019-2020 Varjo Technologies Oy. All rights reserved.

#pragma once

#include <string>
#include <vector>

#include <Varjo_mr.h>

#ifdef USE_EXPERIMENTAL_API
#include <Varjo_types_mr_experimental.h>
#endif

#include "Globals.hpp"

namespace VarjoExamples
{
//! Used to store cached information about a camera property
struct CameraPropertyInfo {
    varjo_CameraPropertyConfigType configType{varjo_CameraPropertyConfigType_List};  //!< Configuration type: range or list
    std::vector<varjo_CameraPropertyMode> supportedModes;                            //!< All supported modes
    std::vector<varjo_CameraPropertyValue> supportedValues;                          //!< All supported values
    varjo_CameraPropertyMode curMode{varjo_CameraPropertyMode_Off};                  //!< Currently set mode
    varjo_CameraPropertyValue curValue{};                                            //!< Currently set value
};

//! Simple example class for managing Varjo mixed reality camera
class CameraManager
{
public:
    //! Construct camera manager
    CameraManager(varjo_Session* session);

    //! Convert given property type to string
    static std::string propertyTypeToString(varjo_CameraPropertyType propertyType, bool brief = false);

    //! Convert given property mode to string
    static std::string propertyModeToString(varjo_CameraPropertyMode propertyMode);

    //! Convert given property value to string
    static std::string propertyValueToString(varjo_CameraPropertyValue propertyValue);

    //! Convert given property value to string
    static std::string propertyValueToString(varjo_CameraPropertyType propertyType, varjo_CameraPropertyValue propertyValue);

    //! Print out currently applied camera configuration
    void printCurrentPropertyConfig() const;

    //! Print out all supported camera properties
    void printSupportedProperties() const;

    //! Enumerate and update cached information about camera properties
    void enumerateCameraProperties(bool mrAvailable);

    //! Set given property to auto mode
    void setAutoMode(varjo_CameraPropertyType propertyType);

    //! Set camera property mode
    void setMode(varjo_CameraPropertyType propertyType, varjo_CameraPropertyMode mode);

    //! Set camera property value
    void setValue(varjo_CameraPropertyType propertyType, varjo_CameraPropertyValue value);

    //! Set camera property to next available mode/value
    void applyNextModeOrValue(varjo_CameraPropertyType type);

    //! Reset all properties to default values
    void resetPropertiesToDefaults();

    //! Update any information for a changed camera property.
    void onCameraPropertyChanged(varjo_CameraPropertyType type);

    //! Get supported property types
    const std::vector<varjo_CameraPropertyType>& getPropertyTypes() const;

    //! Get information about a camera property
    const CameraPropertyInfo& getPropertyInfo(varjo_CameraPropertyType propertyType) const;

    //! Get camera property mode and value as string
    std::string getPropertyAsString(varjo_CameraPropertyType type) const;


private:
    //! Get list of available property modes for given property type
    std::vector<varjo_CameraPropertyMode> getPropertyModeList(varjo_CameraPropertyType propertyType) const;

    //! Get list of available property values for given property type
    std::vector<varjo_CameraPropertyValue> getPropertyValueList(varjo_CameraPropertyType propertyType) const;

    //! Print out supported property modes and values for given property type
    void printSupportedPropertyModesAndValues(varjo_CameraPropertyType propertyType) const;

    //! Find index for given mode in property modes list
    int findPropertyModeIndex(varjo_CameraPropertyMode mode, const std::vector<varjo_CameraPropertyMode>& modes) const;

    //! Find index for given value in property values list
    int findPropertyValueIndex(varjo_CameraPropertyValue propertyValue, const std::vector<varjo_CameraPropertyValue>& values) const;

    //! Set property value of given property type to given index (wrap around)
    void setPropertyValueToModuloIndex(varjo_CameraPropertyType propertyType, int index);

    //! Set property mode of given property type to given index (wrap around)
    void setPropertyModeToModuloIndex(varjo_CameraPropertyType type, int index);

    //! Update the cached status of the given property
    void updatePropertyStatus(varjo_CameraPropertyType type);

private:
    varjo_Session* m_session{nullptr};                                                       //!< Varjo session pointer
    std::vector<varjo_CameraPropertyType> m_propertyTypes;                                   //!< List of available property types.
    std::unordered_map<varjo_CameraPropertyType, CameraPropertyInfo> m_cameraPropertyInfos;  //!< Cached information about properties
};

}  // namespace VarjoExamples
