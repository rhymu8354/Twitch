#ifndef TWITCH_CONNECTION_HPP
#define TWITCH_CONNECTION_HPP

/**
 * @file Connection.hpp
 *
 * This module declares the Twitch::Connection interface.
 *
 * © 2016-2018 by Richard Walters
 */

#include <functional>
#include <memory>
#include <string>

namespace Twitch {

    /**
     * This interface is required by the Messaging class in order to
     * communicate with the Twitch server.  It represents the network
     * connection between the client and server.
     */
    class Connection {
    public:
        // Types

        /**
         * This is the type of function to call whenever a message is
         * received from the Twitch server.
         *
         * @param[in] message
         *     This is the message received from the Twitch server.
         */
        typedef std::function< void(const std::string& message) > MessageReceivedDelegate;

        // Methods

        /**
         * This method is called to set up a callback to happen whenever
         * any message is received from the Twitch server for the user agent.
         *
         * @param[in] messageReceivedDelegate
         *     This is the function to call whenever any message is received
         *     from the Twitch server for the user agent.
         */
        virtual void SetMessageReceivedDelegate(MessageReceivedDelegate messageReceivedDelegate) = 0;

        /**
         * This method is called to establish a connection to the Twitch chat
         * server.  This is a synchronous call; the connection will either
         * succeed or fail before the method returns.
         *
         * @return
         *     An indication of whether or not the connection was successful
         *     is returned.
         */
        virtual bool Connect() = 0;

        /**
         * This method is called to break an existing connection to the Twitch
         * chat server.  This is a synchronous call; the connection will be
         * disconnected before the method returns.
         */
        virtual void Disconnect() = 0;

        /**
         * This method queues the given message to be sent to the Twitch
         * server.  This is an asynchronous call; the message may or may not
         * be sent before the method returns.
         *
         * @param[in] message
         *     This is the text to send.
         */
        virtual void Send(const std::string& message) = 0;
    };

}

#endif /* TWITCH_CONNECTION_HPP */
