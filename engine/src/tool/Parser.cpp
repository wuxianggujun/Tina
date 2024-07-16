//
// Created by wuxianggujun on 2024/7/14.
//

#include "Parser.hpp"

namespace Tina
{
    auto detail::get_internal_id() -> std::uint64_t
    {
        static std::uint64_t id = 0;
        return id++;
    }

    Parser::cmd_base::cmd_base(const std::string& name, const std::string& alternative, const std::string& description,
                               bool required, bool dominant, bool isVariadic):
        name(name), command(!name.empty() ? "-" + name : ""),
        description(description), required(required), handled(false), arguments({}), dominant(dominant),
        variadic(isVariadic)
    {
    }

    Parser::cmd_base::~cmd_base() = default;

    auto Parser::cmd_base::is(const std::string& given) const -> bool
    {
        return given == command || given == alternative;
    }

    auto Parser::parse(const std::vector<std::string>& elements, const int&, int numberBase) -> int
    {
        if (elements.size() != 1)
        {
            throw std::bad_cast();
        }
        return std::stoi(elements[0], nullptr, numberBase);
    }

    auto Parser::parse(const std::vector<std::string>& elements, const bool& defval) -> bool
    {
        if (!elements.empty())
        {
            throw std::runtime_error("A boolean command line parameter cannot have any arguments.");
        }
        return !defval;
    }

    auto Parser::parse(const std::vector<std::string>& elements, const double&) -> double
    {
        if (elements.size() != 1)
        {
            throw std::bad_cast();
        }

        return std::stod(elements[0]);
    }

    auto Parser::parse(const std::vector<std::string>& elements, const float&) -> float
    {
        if(elements.size() != 1)
        {
            throw std::bad_cast();
        }

        return std::stof(elements[0]);
    }

    auto Parser::parse(const std::vector<std::string>& elements, const long double&) -> long double
    {
        if(elements.size() != 1)
        {
            throw std::bad_cast();
        }

        return std::stold(elements[0]);
    }

    auto Parser::parse(const std::vector<std::string>& elements, const unsigned int&, int numberBase) -> unsigned int
    {
        if(elements.size() != 1)
        {
            throw std::bad_cast();
        }

        return static_cast<unsigned int>(std::stoul(elements[0], nullptr, numberBase));
    }

    auto Parser::parse(const std::vector<std::string>& elements, const unsigned long&, int numberBase) -> unsigned long
    {
        if(elements.size() != 1)
        {
            throw std::bad_cast();
        }

        return std::stoul(elements[0], nullptr, numberBase);
    }

    auto Parser::parse(const std::vector<std::string>& elements, const long&) -> long
    {
        if(elements.size() != 1)
        {
            throw std::bad_cast();
        }

        return std::stol(elements[0]);
    }

    auto Parser::parse(const std::vector<std::string>& elements, const std::string&) -> std::string
    {
        if(elements.size() != 1)
        {
            throw std::bad_cast();
        }

        return elements[0];
    }

    


    
} // Tina
