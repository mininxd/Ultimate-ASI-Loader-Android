#pragma once

#include <string>
#include <map>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace ual::android
{
    class IniReader
    {
    public:
        using SectionMap = std::map<std::string, std::string>;
        using DataMap = std::map<std::string, SectionMap>;

    private:
        DataMap data_;

        static inline std::string Trim(const std::string& str)
        {
            size_t first = str.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) return "";
            size_t last = str.find_last_not_of(" \t\r\n");
            return str.substr(first, (last - first + 1));
        }

        static inline std::string ToLower(std::string str)
        {
            std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) {
                return std::tolower(c);
            });
            return str;
        }

    public:
        bool LoadFile(const std::string& filepath)
        {
            std::ifstream file(filepath);
            if (!file.is_open()) return false;

            std::string line;
            std::string currentSection = "globalsets";

            while (std::getline(file, line))
            {
                line = Trim(line);
                if (line.empty() || line[0] == ';' || line[0] == '#') continue;

                if (line.front() == '[' && line.back() == ']')
                {
                    currentSection = ToLower(Trim(line.substr(1, line.size() - 2)));
                }
                else
                {
                    size_t sep = line.find('=');
                    if (sep != std::string::npos)
                    {
                        std::string key = ToLower(Trim(line.substr(0, sep)));
                        std::string val = Trim(line.substr(sep + 1));
                        data_[currentSection][key] = val;
                    }
                }
            }
            return true;
        }

        std::string GetString(const std::string& section, const std::string& key, const std::string& defaultVal = "") const
        {
            auto secIt = data_.find(ToLower(section));
            if (secIt != data_.end())
            {
                auto keyIt = secIt->second.find(ToLower(key));
                if (keyIt != secIt->second.end())
                {
                    return keyIt->second;
                }
            }
            return defaultVal;
        }

        bool GetBoolean(const std::string& section, const std::string& key, bool defaultVal = false) const
        {
            std::string val = ToLower(GetString(section, key));
            if (val.empty()) return defaultVal;
            if (val == "1" || val == "true" || val == "yes" || val == "on") return true;
            if (val == "0" || val == "false" || val == "no" || val == "off") return false;
            return defaultVal;
        }

        int GetInteger(const std::string& section, const std::string& key, int defaultVal = 0) const
        {
            std::string val = GetString(section, key);
            if (val.empty()) return defaultVal;
            try
            {
                return std::stoi(val);
            }
            catch (...)
            {
                return defaultVal;
            }
        }
    };
}
