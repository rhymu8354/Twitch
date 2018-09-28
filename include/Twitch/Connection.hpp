#ifndef TWITCH_CONNECTION_HPP
#define TWITCH_CONNECTION_HPP

/**
 * @file Connection.hpp
 *
 * This module declares the Twitch::Connection interface.
 *
 * © 2016-2018 by Richard Walters
 */

#include <memory>

namespace Twitch {

    /**
     * This interface is required by the Messaging class in order to
     * communicate with the Twitch server.  It represents the network
     * connection between the client and server.
     */
    class Connection {
    public:
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
