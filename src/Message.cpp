/**
 * @file Message.cpp
 *
 * This module contains the implementation of the Twitch::Message structure.
 *
 * © 2018 by Richard Walters
 */

#include "Message.hpp"

#include <inttypes.h>
#include <stdio.h>
#include <SystemAbstractions/StringExtensions.hpp>

namespace {

    /**
     * This is the required line terminator for lines of text
     * sent to or from Twitch chat servers.
     */
    const std::string CRLF = "\r\n";

    /**
     * This is a helper function which parses the tags string from a raw Twitch
     * message and stores them in the given message.
     *
     * @param[in] unparsedTags
     *     This is the raw string containing the tags for the given message.
     *
     * @return
     *     The tags parsed from the given raw tags string is returned.
     */
    Twitch::Messaging::TagsInfo ParseTags(const std::string& unparsedTags) {
        Twitch::Messaging::TagsInfo parsedTags;
        const auto tags = SystemAbstractions::Split(unparsedTags, ';');
        for (const auto& tag: tags) {
            const auto nameValuePair = SystemAbstractions::Split(tag, '=');
            if (nameValuePair.size() != 2) {
                continue;
            }
            const auto& name = nameValuePair[0];
            const auto& value = nameValuePair[1];
            parsedTags.allTags[name] = value;
            if (name == "badges") {
                const auto badges = SystemAbstractions::Split(value, ',');
                for (const auto& badge: badges) {
                    (void)parsedTags.badges.insert(badge);
                }
            } else if (name == "color") {
                (void)sscanf(
                    value.c_str(),
                    "#%" SCNx32,
                    &parsedTags.color
                );
            } else if (name == "display-name") {
                parsedTags.displayName = value;
            } else if (name == "emotes") {
                const auto emotes = SystemAbstractions::Split(value, '/');
                for (const auto& emote: emotes) {
                    const auto idInstancesPair = SystemAbstractions::Split(emote, ':');
                    if (idInstancesPair.size() != 2) {
                        continue;
                    }
                    int id;
                    if (sscanf(idInstancesPair[0].c_str(), "%d", &id) != 1) {
                        continue;
                    }
                    auto& emoteInstances = parsedTags.emotes[id];
                    const auto instances = SystemAbstractions::Split(idInstancesPair[1], ',');
                    for (const auto& instance: instances) {
                        int begin, end;
                        if (sscanf(instance.c_str(), "%d-%d", &begin, &end) != 2) {
                            continue;
                        }
                        emoteInstances.push_back({begin, end});
                    }
                }
            } else if (name == "tmi-sent-ts") {
                uintmax_t timeAsInt;
                if (sscanf(value.c_str(), "%" SCNuMAX, &timeAsInt) == 1) {
                    parsedTags.timestamp = (decltype(parsedTags.timestamp))(timeAsInt / 1000);
                    parsedTags.timeMilliseconds = (decltype(parsedTags.timeMilliseconds))(timeAsInt % 1000);
                } else {
                    parsedTags.timestamp = 0;
                    parsedTags.timeMilliseconds = 0;
                }
            } else if (name == "room-id") {
                if (sscanf(value.c_str(), "%" SCNuMAX, &parsedTags.channelId) != 1) {
                    parsedTags.channelId = 0;
                }
            }
        }
        return parsedTags;
    }

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
            Tags,
            PrefixOrCommandFirstCharacter,
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
        std::string unparsedTags;
        while (offset < line.length()) {
            switch (state) {
                // First character of the line.  It could be ':',
                // which signals a prefix, or it's the first character
                // of the command.
                case State::LineFirstCharacter: {
                    if (line[offset] == '@') {
                        state = State::Tags;
                    } else if (line[offset] == ':') {
                        state = State::Prefix;
                    } else {
                        state = State::CommandNotFirstCharacter;
                        message.command += line[offset];
                    }
                } break;

                // Tags
                case State::Tags: {
                    if (line[offset] == ' ') {
                        state = State::PrefixOrCommandFirstCharacter;
                    } else {
                        unparsedTags += line[offset];
                    }
                } break;

                // Prefix marker or first character of command
                case State::PrefixOrCommandFirstCharacter: {
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
            || (state == State::Tags)
            || (state == State::PrefixOrCommandFirstCharacter)
            || (state == State::Prefix)
            || (state == State::CommandFirstCharacter)
        ) {
            message.command.clear();
        }
        message.tags = ParseTags(unparsedTags);
        return true;
    }

}
