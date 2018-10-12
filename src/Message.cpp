/**
 * @file Message.cpp
 *
 * This module contains the implementation of the Twitch::Message structure.
 *
 * © 2018 by Richard Walters
 */

#include "Message.hpp"

namespace {

    /**
     * This is the required line terminator for lines of text
     * sent to or from Twitch chat servers.
     */
    const std::string CRLF = "\r\n";

}

namespace Twitch {

    bool Message::Parse(
        std::string& dataReceived,
        Message& message,
        SystemAbstractions::DiagnosticsSender& diagnosticsSender
    ) {
        // This tracks the current state of the state machine used
        // in this function to parse the raw text of the message.
        enum class State {
            LineFirstCharacter,
            Prefix,
            CommandFirstCharacter,
            CommandNotFirstCharacter,
            ParameterFirstCharacter,
            ParameterNotFirstCharacter,
            Trailer,
        } state = State::LineFirstCharacter;

        // Extract the next line.
        const auto lineEnd = dataReceived.find(CRLF);
        if (lineEnd == std::string::npos) {
            return false;
        }
        const auto line = dataReceived.substr(0, lineEnd);
        diagnosticsSender.SendDiagnosticInformationString(0, "> " + line);

        // Remove the line from the buffer.
        dataReceived = dataReceived.substr(lineEnd + CRLF.length());

        // Unpack the message from the line.
        size_t offset = 0;
        message = Message();
        while (offset < line.length()) {
            switch (state) {
                // First character of the line.  It could be ':',
                // which signals a prefix, or it's the first character
                // of the command.
                case State::LineFirstCharacter: {
                    if (line[offset] == ':') {
                        state = State::Prefix;
                    } else {
                        state = State::CommandNotFirstCharacter;
                        message.command += line[offset];
                    }
                } break;

                // Prefix
                case State::Prefix: {
                    if (line[offset] == ' ') {
                        state = State::CommandFirstCharacter;
                    } else {
                        message.prefix += line[offset];
                    }
                } break;

                // First character of command
                case State::CommandFirstCharacter: {
                    if (line[offset] != ' ') {
                        state = State::CommandNotFirstCharacter;
                        message.command += line[offset];
                    }
                } break;

                // Command
                case State::CommandNotFirstCharacter: {
                    if (line[offset] == ' ') {
                        state = State::ParameterFirstCharacter;
                    } else {
                        message.command += line[offset];
                    }
                } break;

                // First character of parameter
                case State::ParameterFirstCharacter: {
                    if (line[offset] == ':') {
                        state = State::Trailer;
                        message.parameters.push_back("");
                    } else if (line[offset] != ' ') {
                        state = State::ParameterNotFirstCharacter;
                        message.parameters.push_back(line.substr(offset, 1));
                    }
                } break;

                // Parameter (not last, or last having no spaces)
                case State::ParameterNotFirstCharacter: {
                    if (line[offset] == ' ') {
                        state = State::ParameterFirstCharacter;
                    } else {
                        message.parameters.back() += line[offset];
                    }
                } break;

                // Last Parameter (may include spaces)
                case State::Trailer: {
                    message.parameters.back() += line[offset];
                } break;
            }
            ++offset;
        }
        if (
            (state == State::LineFirstCharacter)
            || (state == State::Prefix)
            || (state == State::CommandFirstCharacter)
        ) {
            message.command.clear();
        }
        return true;
    }

}
