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
#include <SystemAbstractions/DiagnosticsSender.hpp>

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
         * This contains all the information about a message received in a
         * channel.
         */
        struct MessageInfo {
            /**
             * This is the channel to which the message was sent.
             */
            std::string channel;

            /**
             * This is the user who sent the message.
             */
            std::string user;

            /**
             * This is the content of the message.
             */
            std::string message;
        };

        /**
         * This contains all the information about a whisper received.
         */
        struct WhisperInfo {
            /**
             * This is the user who sent the message.
             */
            std::string user;

            /**
             * This is the content of the message.
             */
            std::string message;
        };

        /**
         * This contains all the information about a membership command
         * received.
         */
        struct MembershipInfo {
            /**
             * This is the channel whose membership changed.
             */
            std::string channel;

            /**
             * This is the user whose membership to the channel changed.
             */
            std::string user;
        };

        /**
         * This is a base class and interface to be implemented by the user of
         * this class, in order to receive notifications, events, and other
         * callbacks from the class.
         *
         * By default, the base class provides an implementation of each
         * method which essentially does nothing.  The derived class need not
         * override methods it doesn't care about.
         */
        class User {
        public:
            /**
             * This is called to notify the user when the user agent has
             * successfully logged into the Twitch server.
             */
            virtual void LogIn() {
            }

            /**
             * This is called when the user agent completes logging out of the
             * Twitch server, or when the connection is closed, or could not
             * be established in the first place.
             */
            virtual void LogOut() {
            }

            /**
             * This is called whenever a user joins a channel.
             *
             * @param[in] membershipInfo
             *     This holds the information about which user and channel
             *     were involved.
             */
            virtual void Join(MembershipInfo&& membershipInfo) {
            }

            /**
             * This is called whenever a user leaves a channel.
             *
             * @param[in] membershipInfo
             *     This holds the information about which user and channel
             *     were involved.
             */
            virtual void Leave(MembershipInfo&& membershipInfo) {
            }

            /**
             * This is called whenever the user receives a message sent to a
             * channel.
             *
             * @param[in] messageInfo
             *     This contains all the information about the received
             *     message.
             */
            virtual void Message(MessageInfo&& messageInfo) {
            }

            /**
             * This is called whenever the user receives a whisper.
             *
             * @param[in] whisperInfo
             *     This contains all the information about the received
             *     whisper.
             */
            virtual void Whisper(WhisperInfo&& whisperInfo) {
            }

            /**
             * This is called whenever the user receives a notice from the
             * server.
             *
             * @param[in] message
             *     This is the text of the server notice message.
             */
            virtual void Notice(const std::string& message) {
            }
        };

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
         * This method forms a new subscription to diagnostic
         * messages published by the transport.
         *
         * @param[in] delegate
         *     This is the function to call to deliver messages
         *     to the subscriber.
         *
         * @param[in] minLevel
         *     This is the minimum level of message that this subscriber
         *     desires to receive.
         *
         * @return
         *     A function is returned which may be called
         *     to terminate the subscription.
         */
        SystemAbstractions::DiagnosticsSender::UnsubscribeDelegate SubscribeToDiagnostics(
            SystemAbstractions::DiagnosticsSender::DiagnosticMessageDelegate delegate,
            size_t minLevel = 0
        );

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
         * This method is called to set the object which will receive
         * any notifications, events, or callbacks of interest to the user.
         *
         * @param[in] user
         *     This is the object provided by the user of this class,
         *     in order to receive notifications, events, and other callbacks
         *     from the class.
         */
        void SetUser(std::shared_ptr< User > user);

        /**
         * This method starts the process of logging into the Twitch server as
         * a registered user/bot.
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
         * This method starts the process of logging into the Twitch server
         * anonymously.
         */
        void LogInAnonymously();

        /**
         * This method starts the process of logging out of the Twitch server.
         *
         * @param[in] farewell
         *     This is the message to include in the command sent to the
         *     Twitch server just before the connection is closed.
         */
        void LogOut(const std::string& farewell);

        /**
         * This method starts the process of joining a Twitch chat channel.
         *
         * @param[in] channel
         *     This is the name of the channel to join.
         */
        void Join(const std::string& channel);

        /**
         * This method starts the process of leaving a Twitch chat channel.
         *
         * @param[in] channel
         *     This is the name of the channel to leave.
         */
        void Leave(const std::string& channel);

        /**
         * This method sends a message to Twitch chat channel.
         *
         * @param[in] channel
         *     This is the name of the channel to which to send the message.
         *
         * @param[in] message
         *     This is the content of the message to send.
         */
        void SendMessage(
            const std::string& channel,
            const std::string& message
        );

        /**
         * This method sends a whsper to another Twitch user.
         *
         * @param[in] nickname
         *     This is the nickname of the other Twitch user to whisper.
         *
         * @param[in] message
         *     This is the content of the whisper to send.
         */
        void SendWhisper(
            const std::string& nickname,
            const std::string& message
        );

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
