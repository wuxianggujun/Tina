//
// Created by wuxianggujun on 2024/7/14.
//

#include "Parser.hpp"

#include <utility>

namespace Tina
{
    auto detail::get_internal_id() -> std::uint64_t
    {
        static std::uint64_t id = 0;
        return id++;
    }

    Parser::cmd_base::cmd_base(const std::string& name, const std::string& alternative, std::string description,
                               bool required, bool dominant, bool isVariadic):
        name(name), command(!name.empty() ? "-" + name : ""),
        description(std::move(description)), required(required), handled(false), arguments({}), dominant(dominant),
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
        if (elements.size() != 1)
        {
            throw std::bad_cast();
        }

        return std::stof(elements[0]);
    }

    auto Parser::parse(const std::vector<std::string>& elements, const long double&) -> long double
    {
        if (elements.size() != 1)
        {
            throw std::bad_cast();
        }

        return std::stold(elements[0]);
    }

    auto Parser::parse(const std::vector<std::string>& elements, const unsigned int&, int numberBase) -> unsigned int
    {
        if (elements.size() != 1)
        {
            throw std::bad_cast();
        }

        return static_cast<unsigned int>(std::stoul(elements[0], nullptr, numberBase));
    }

    auto Parser::parse(const std::vector<std::string>& elements, const unsigned long&, int numberBase) -> unsigned long
    {
        if (elements.size() != 1)
        {
            throw std::bad_cast();
        }

        return std::stoul(elements[0], nullptr, numberBase);
    }

    auto Parser::parse(const std::vector<std::string>& elements, const long&) -> long
    {
        if (elements.size() != 1)
        {
            throw std::bad_cast();
        }

        return std::stol(elements[0]);
    }

    auto Parser::parse(const std::vector<std::string>& elements, const std::string&) -> std::string
    {
        if (elements.size() != 1)
        {
            throw std::bad_cast();
        }

        return elements[0];
    }

    auto Parser::stringify(const std::string& value) -> std::string
    {
        return value;
    }

    Parser::Parser(int argc, const char** argv): appname_(argv[0])
    {
        for (int i = 1; i < argc; i++)
        {
            arguments_.emplace_back(argv[i]);
        }
        enable_help();
    }

    Parser::Parser(int argc, char** argv) : appname_(argv[0])
    {
        for (int i = 1; i < argc; i++)
        {
            arguments_.emplace_back(argv[i]);
        }
        enable_help();
    }

    auto Parser::has_help() const -> bool
    {
        for (const auto& cmd : commands_)
        {
            const auto& command = cmd.second;
            if (command->name == "h" && command->alternative == "--help")
            {
                return true;
            }
        }
        return false;
    }

    void Parser::enable_help()
    {
        set_callback("h", "help", std::function<bool(callback_args&)>([this](callback_args& args)
        {
            args.output << this->usage();
            return false;
        }), "", true);
    }

    void Parser::disable_help()
    {
        for (auto command = commands_.begin(); command != commands_.end(); ++command)
        {
            if (command->second->name == "h" && command->second->alternative == "--help")
            {
                commands_.erase(command);
                break;
            }
        }
    }

    void Parser::reset()
    {
        commands_.clear();
        commands_.shrink_to_fit();
    }

    void Parser::run_and_exit_if_error()
    {
        if (!run())
        {
            exit(1);
        }
    }

    auto Parser::run() -> bool
    {
        return run(std::cout, std::cerr);
    }

    auto Parser::run(std::ostream& output) -> bool
    {
        return run(output, std::cerr);
    }

    auto Parser::run(std::ostream& output, std::ostream& error) -> bool
    {
        if (!arguments_.empty())
        {
            auto current =  find_default();

            for(const auto& arg : arguments_)
            {

                auto isarg = !arg.empty() && arg[0] == '-';
                auto associated = isarg ? find(arg) : nullptr;

                if (associated != nullptr)
                {
                    current = associated;
                    associated->handled = true;
                }else if (current == nullptr)
                {
                    error << no_default();
                    return false;
                }else
                {
                    current->arguments.push_back(arg);
                    current->handled = true;

                    if (!current->variadic)
                    {
                        current = find_default();
                    }
                    
                }
                
            }
        }

        for (auto& command_pait : commands_)
        {
            auto& command = command_pait.second;
            if (command->handled && command->dominant && !command->parse(output, error))
            {
                error << howto_use(command);
                return false;
            }
        }


        // Next, check for any missing arguments.
        for(const auto& command_pair : commands_)
        {
            const auto& command = command_pair.second;
            if(command->required && !command->handled)
            {
                error << howto_required(command);
                return false;
            }
        }

        // Finally, parse all remaining arguments.
        for(auto& command_pair : commands_)
        {
            auto& command = command_pair.second;
            if(command->handled && !command->dominant && !command->parse(output, error))
            {
                error << howto_use(command);
                return false;
            }
        }

        return true;
    }


    auto Parser::requirements() const -> int
    {
        int count = 0;

        for(const auto& command_pair : commands_)
        {
            const auto& command = command_pair.second;
            if(command->required)
            {
                ++count;
            }
        }

        return count;
    }

    auto Parser::commands() const -> int
    {
        return static_cast<int>(commands_.size());
    }

    auto Parser::app_name() const -> const std::string&
    {
        return appname_;
    }

    auto Parser::usage() const -> std::string
{
    std::stringstream ss{};
    ss << "Available parameters:\n\n";

    for(const auto& command_pair : commands_)
    {
        const auto& command = command_pair.second;

        ss << "  " << command->command << "\t" << command->alternative;

        if(command->required)
        {
            ss << "\t(required)";
        }

        ss << "\n   " << command->description;

        if(!command->required)
        {
            ss << "\n   "
               << "This parameter is optional. The default value is '" + command->printValue() << "'.";
        }

        ss << "\n\n";
    }

    return ss.str();
}

void Parser::print_help(std::stringstream& ss) const
{
    if(has_help())
    {
        ss << "For more help use --help or -h.\n";
    }
}

auto Parser::howto_required(const std::unique_ptr<cmd_base>& command) const -> std::string
{
    std::stringstream ss{};
    ss << "The parameter " << command->name << " is required.\n";
    ss << command->description << '\n';
    print_help(ss);
    return ss.str();
}

auto Parser::howto_use(const std::unique_ptr<cmd_base>& command) const -> std::string
{
    std::stringstream ss{};
    ss << "The parameter " << command->name << " has invalid arguments.\n";
    ss << command->description << '\n';
    print_help(ss);
    return ss.str();
}

auto Parser::no_default() const -> std::string
{
    std::stringstream ss{};
    ss << "No default parameter has been specified.\n";
    ss << "The given argument must be used with a parameter.\n";
    print_help(ss);
    return ss.str();
}

auto Parser::find_default() -> cmd_base*
{
    for(auto& command_pair : commands_)
    {
        auto& command = command_pair.second;
        if(command->name.empty())
        {
            return command.get();
        }
    }

    return nullptr;
}

auto Parser::find(const std::string& name) -> cmd_base*
{
    for(auto& command_pair : commands_)
    {
        auto& command = command_pair.second;
        if(command->is(name))
        {
            return command.get();
        }
    }

    return nullptr;
}
    
} // Tina
