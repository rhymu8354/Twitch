#ifndef TWITCH_MESSAGE_HPP
#define TWITCH_MESSAGE_HPP

/**
 * @file Message.hpp
 *
 * This module declares the Twitch::Message structure.
 *
 * © 2018 by Richard Walters
 */

#include <string>
#include <SystemAbstractions/DiagnosticsSender.hpp>
#include <vector>

namespace Twitch {

    /**
     * This contains all the information parsed from a single message
     * from the Twitch server.
     */
    struct Message {
        // Properties

        /**
         * If this is not an empty string, the message included a prefix,
         * which is stored here, without the leading colon (:) character.
         */
        std::string prefix;

        /**
         * This is the command portion of the message, which may be
         * a three-digit code, or an IRC command name.
         *
         * If it's empty, the message was invalid, or there was no message.
         */
        std::string command;

        /**
         * These are the parameters, if any, provided in the message.
         */
        std::vector< std::string > parameters;

        // Methods

        /**
         * This method extracts the next message received from the
         * Twitch server.
         *
         * @param[in,out] dataReceived
         *     This is essentially just a buffer to receive raw characters
         *     from the Twitch server, until a complete line has been
         *     received, removed from this buffer, and handled appropriately.
         *
         * @param[out] message
         *     This is where to store the next message received from the
         *     Twitch server.
         *
         * @param[in] diagnosticsSender
         *     This is used to publish any diagnostic messages generated
         *     by this function.
         *
         * @return
         *     An indication of whether or not a complete line was
         *     extracted is returned.
         */
        static bool Parse(
            std::string& dataReceived,
            Message& message,
            SystemAbstractions::DiagnosticsSender& diagnosticsSender
        );

    };

}

#endif /* TWITCH_MESSAGE_HPP */
