#ifndef TWITCH_MESSAGING_HPP
#define TWITCH_MESSAGING_HPP

/**
 * @file Messaging.hpp
 *
 * This module declares the Twitch::Messaging functions.
 *
 * © 2016-2018 by Richard Walters
 */

#include "Connection.hpp"
#include "TimeKeeper.hpp"

#include <functional>
#include <memory>
#include <string>

namespace Twitch {

    /**
     * This class represents a user agent for connecting to the messaging
     * interfaces of Twitch, for doing things such as connecting to chat,
     * receiving chat messages, and sending chat messages.
     */
    class Messaging {
        // Types
    public:
        /**
         * This is the type of function used by the class to create
         * new connections to the Twitch server.
         */
        typedef std::function< std::shared_ptr< Connection >() > ConnectionFactory;

        /**
         * This is the type of function used to notify the user
         * when the agent has successfully logged into the Twitch server.
         */
        typedef std::function< void() > LoggedInDelegate;

        /**
         * This is the function to call when the user agent completes
         * logging out of the Twitch server, or when the connection
         * is closed, or could not be established in the first place.
         */
        typedef std::function< void() > LoggedOutDelegate;

        // Lifecycle management
    public:
        ~Messaging() noexcept;
        Messaging(const Messaging& other) = delete;
        Messaging(Messaging&&) noexcept = delete;
        Messaging& operator=(const Messaging& other) = delete;
        Messaging& operator=(Messaging&&) noexcept = delete;

        // Public methods
    public:
        /**
         * This is the default constructor.
         */
        Messaging();

        /**
         * This method is used to provide the class with a means of
         * establishing connections to the Twitch server.
         *
         * @param[in] connectionFactory
         *     This is the function to call in order to make a new
         *     connection to the Twitch server.
         */
        void SetConnectionFactory(ConnectionFactory connectionFactory);

        /**
         * This method is used to provide the class with a means of
         * measuring elapsed time periods.
         *
         * @param[in] timeKeeper
         *     This is the object to use to measure elapsed time periods.
         */
        void SetTimeKeeper(std::shared_ptr< TimeKeeper > timeKeeper);

        /**
         * This method is called to set up a callback to happen when
         * the user agent successfully logs into the Twitch server.
         *
         * @param[in] loggedInDelegate
         *     This is the function to call when the user agent successfully
         *     logs into the Twitch server.
         */
        void SetLoggedInDelegate(LoggedInDelegate loggedInDelegate);

        /**
         * This method is called to set up a callback to happen when
         * the user agent completes logging out of the Twitch server.
         *
         * @param[in] loggedOutDelegate
         *     This is the function to call when the user agent completes
         *     logging out of the Twitch server.
         */
        void SetLoggedOutDelegate(LoggedOutDelegate loggedOutDelegate);

        /**
         * This method starts the process of logging into the Twitch server.
         *
         * @param[in] nickname
         *     This is the nickname to use when logging into chat.
         *     It should be the same as the user's Twitch account,
         *     except all in lower-case.
         *
         * @param[in] token
         *     This is the OAuth token to use to authenticate with
         *     the Twitch server.
         */
        void LogIn(
            const std::string& nickname,
            const std::string& token
        );

        /**
         * This method starts the process of logging out of the Twitch server.
         *
         * @param[in] farewell
         *     This is the message to include in the command sent to the
         *     Twitch server just before the connection is closed.
         */
        void LogOut(const std::string& farewell);

        // Private properties
    private:
        /**
         * This is the type of structure that contains the private
         * properties of the instance.  It is defined in the implementation
         * and declared here to ensure that it is scoped inside the class.
         */
        struct Impl;

        /**
         * This contains the private properties of the instance.
         */
        std::unique_ptr< Impl > impl_;
    };

}

#endif /* TWITCH_MESSAGING_HPP */
