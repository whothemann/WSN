#pragma once
#include <QString>

/**
 * @brief Enumeration of supported device types in the network.
 */
enum class DeviceType {
    Gateway,        /**< Gateway/root node */
    AccessPoint,    /**< Access point (e.g. ACCESS, ACCESS1, ACCESS2) */
    Bed,            /**< Bed node (e.g. BED1, BED2) */
    Nurse,          /**< Nurse node */
    Unknown         /**< Unknown or unsupported device */
};

/**
 * @brief Determines the device type from a role string.
 *
 * The comparison is case-insensitive.
 *
 * @param s Role string (e.g. "GATEWAY", "ACCESS2", "BED3")
 * @return Corresponding DeviceType
 */
inline DeviceType deviceTypeFromString(const QString &s)
{
    const QString up = s.toUpper();

    if (up == "GATEWAY") return DeviceType::Gateway;
    if (up.startsWith("ACCESS")) return DeviceType::AccessPoint;
    if (up == "NURSE") return DeviceType::Nurse;
    if (up.startsWith("BED")) return DeviceType::Bed;

    return DeviceType::Unknown;
}

/**
 * @brief Extracts the bed number from a role string.
 *
 * Example:
 * - "BED3" → 3
 * - "BED"  → -1
 *
 * @param role Role string
 * @return Bed number on success, -1 on failure
 */
inline int bedNumberFromRole(const QString &role)
{
    const QString up = role.toUpper();
    if (!up.startsWith("BED"))
        return -1;

    const QString suffix = up.mid(3);
    bool ok = false;
    int n = suffix.toInt(&ok);
    return ok ? n : -1;
}

/**
 * @brief Extracts the access point number from a role string.
 *
 * Examples:
 * - "ACCESS"  → 0
 * - "ACCESS1" → 1
 * - "ACCESS2" → 2
 *
 * @param role Role string
 * @return Access point number, or -1 if invalid
 */
inline int accessNumberFromRole(const QString &role)
{
    const QString up = role.toUpper();
    if (!up.startsWith("ACCESS"))
        return -1;

    if (up.length() == 6)
        return 0;

    const QString suffix = up.mid(6);
    bool ok = false;
    int n = suffix.toInt(&ok);
    return ok ? n : -1;
}

/**
 * @brief Converts a DeviceType value to a human-readable string.
 *
 * @param t DeviceType value
 * @return Descriptive string
 */
inline QString deviceTypeToString(DeviceType t)
{
    switch (t) {
    case DeviceType::Gateway:     return "Gateway";
    case DeviceType::AccessPoint: return "Access Point";
    case DeviceType::Bed:         return "Bed";
    case DeviceType::Nurse:       return "Nurse";
    default:                      return "Unknown";
    }
}
