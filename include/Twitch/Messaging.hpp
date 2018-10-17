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
#include <map>
#include <set>
#include <string>
#include <SystemAbstractions/DiagnosticsSender.hpp>
#include <vector>

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
         * This contains information about the tags of a message.
         */
        struct TagsInfo {
            /**
             * This is the name of the user as it should be displayed in the
             * user interface, with proper capitalization.
             */
            std::string displayName;

            /**
             * This is the set of badges meant to be displayed in front of the
             * user's name.
             */
            std::set< std::string > badges;

            /**
             * This contains information about the emotes used in the message.
             *
             * Each key is the ID of an emote.  One may use this ID to obtain
             * an image corresponding to the emote, using this URL template:
             *   http://static-cdn.jtvnw.net/emoticons/v1/<emote ID>/<size>
             * Where size is 1.0, 2.0 or 3.0.
             * (For more information, see: https://dev.twitch.tv/docs/irc/tags/)
             *
             * Each value is a vector of instances of the emote.  Each instance
             * is a pair consisting of the indecies of the first and last
             * characters corresponding to the emote.
             */
            std::map< int, std::vector< std::pair< int, int > > > emotes;

            /**
             * This is the color in which to draw the user's display name.  The
             * color is in RRGGBB format (24 bits, where bits 23-16 correspond
             * to the red channel, bits 15-8 correspond to the green channel,
             * and bits 7-0 correspond to the blue channel).
             */
            uint32_t color = 0xFFFFFF;

            /**
             * This is the time, as expressed in seconds past the UNIX epoch (1
             * January 1970, Midnight, UTC), when this message was sent.
             */
            time_t timestamp = 0;

            /**
             * This is the fractional amount of time, in milliseconds, when
             * this message was sent, past the second indicated by the
             * timestamp.
             */
            unsigned int timeMilliseconds = 0;

            /**
             * This is the ID of the channel to which the message was sent.
             */
            uintmax_t channelId = 0;

            /**
             * This is the ID of the user who sent the message.
             */
            uintmax_t userId = 0;

            /**
             * This holds a copy of the names and values of all the tags,
             * including both the ones known about by the parser (above) as
             * well as those not known.
             */
            std::map< std::string, std::string > allTags;
        };

        /**
         * This contains all the information about a message received in a
         * channel.
         */
        struct MessageInfo {
            /**
             * This contains information provided in the message's tags.
             */
            TagsInfo tags;

            /**
             * This is the name of the channel to which the message was sent.
             */
            std::string channel;

            /**
             * This is the name of the user who sent the message.
             */
            std::string user;

            /**
             * This is the content of the message.
             */
            std::string messageContent;

            /**
             * This is the ID the message.
             */
            std::string messageId;

            /**
             * This is the number of bits that were cheered/donated with the
             * message.
             */
            size_t bits = 0;
        };

        /**
         * This contains all the information about a whisper received.
         */
        struct WhisperInfo {
            /**
             * This contains information provided in the message's tags.
             */
            TagsInfo tags;

            /**
             * This is the name of the user who sent the message.
             */
            std::string user;

            /**
             * This is the content of the message.
             */
            std::string message;
        };

        /**
         * This contains all the information about a server notice received.
         */
        struct NoticeInfo {
            /**
             * This is the ID of the message.
             */
            std::string id;

            /**
             * This is the content of the message.
             */
            std::string message;

            /**
             * If the notice was received within the context of a channel, this
             * is the name of the channel.  Otherwise, this will be empty,
             * indicating a global server notice.
             */
            std::string channel;
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
         * This contains all the information about a hosting change.
         */
        struct HostInfo {
            /**
             * This is a flag which indicates whether or not hosting mode has
             * been turned on.
             */
            bool on = false;

            /**
             * This is the name of the channel that is doing the hosting.
             */
            std::string hosting;

            /**
             * This is the name of the channel being hosted, if hosting mode
             * has been turned on.
             */
            std::string beingHosted;

            /**
             * This is the number of viewers from the hosting channel that are
             * visiting the channel being hosted.
             */
            size_t viewers = 0;
        };

        /**
         * This contains all the information about a room mode change.
         */
        struct RoomModeChangeInfo {
            /**
             * This is the mode which changed.
             * - slow: limit on how often users in the chat room are allowed to
             *   send messages
             * - followers-only: restrict chat to all or some of your
             *   followers, based on how long they’ve followed
             * - r9k: disallows users from posting non-unique messages to the
             *   channel
             * - emote-only: only messages that are 100% emotes are allowed
             * - subs-only: only users subscribed to you can talk in the chat
             *   room
             */
            std::string mode;

            /**
             * This is the parameter accompanying the mode change.  The meaning
             * of the parameter depends on the mode.
             * - slow: number of seconds, or 0 if off
             * - followers-only: number of minutes a follower will need to have
             *   been following in order to be able to chat, or -1 if off
             * - r9k: 1 if on, 0 if off
             * - emote-only: 1 if on, 0 if off
             * - subs-only: 1 if on, 0 if off
             */
            int parameter = 0;

            /**
             * This is the channel whose mode changed.
             */
            std::string channelName;

            /**
             * This is the ID of the channel whose mode changed.
             */
            uintmax_t channelId = 0;
        };

        /**
         * This contains all the information about a ban, timeout, our chat
         * clear.
         */
        struct ClearInfo {
            /**
             * This identifies what kind of clear was done.
             */
            enum class Type {
                /**
                 * Clear all messages from chat.
                 */
                ClearAll,

                /**
                 * Remove/delete individual message from chat.
                 */
                ClearMessage,

                /**
                 * A user has been "timed out", meaning they aren't allowed to
                 * chat for some fixed amount of time.
                 */
                Timeout,

                /**
                 * A user has been permanently banned from the channel.
                 */
                Ban,
            } type = Type::ClearAll;

            /**
             * This is the channel in which the ban/timeout/clear occurred.
             */
            std::string channel;

            /**
             * This is the name of the user who was timed out or banned.
             *
             * NOTE: only applies for types Timeout and Ban.
             */
            std::string user;

            /**
             * This is a human-readable string meant to convey an explanation
             * of why the user was timed out or banned.
             *
             * NOTE: only applies for types Timeout and Ban.
             */
            std::string reason;

            /**
             * This is the ID the message that was deleted.
             *
             * NOTE: only applies for ClearMessage type.
             */
            std::string offendingMessageId;

            /**
             * This is a copy of the message that was deleted.
             *
             * NOTE: only applies for ClearMessage type.
             */
            std::string offendingMessageContent;

            /**
             * This is the number of seconds for which the user has been timed
             * out.
             *
             * NOTE: only applies for types Timeout.
             */
            size_t duration = 0;

            /**
             * This contains information provided in the message's tags.
             */
            TagsInfo tags;
        };

        /**
         * This contains all the information about a user's moderator status
         * notification.
         */
        struct ModInfo {
            /**
             * This indicates whether or not the user is a moderator.
             */
            bool mod = false;

            /**
             * This is the channel in which the user's moderator status was
             * announced.
             */
            std::string channel;

            /**
             * This is the name of the user whose moderator status was
             * announced.
             */
            std::string user;
        };

        /**
         * This contains all the information about a user's global or
         * channel-specific status notification.
         */
        struct UserStateInfo {
            /**
             * This flag indicates whether or not the user state applies to
             * them globally, rather than to just one channel.
             */
            bool global = false;

            /**
             * This is the channel for which the state is applicable, if this
             * isn't a global state information notification.
             */
            std::string channel;

            /**
             * This contains information provided in the message's tags.
             */
            TagsInfo tags;
        };

        /**
         * This contains all the information about a user subscription
         * notification.
         */
        struct SubInfo {
            /**
             * This identifies the type of the subscription notification.
             */
            enum class Type {
                /**
                 * Unrecognized type of subscription notification; check
                 * "msg-id" tag.
                 */
                Unknown,

                /**
                 * New subscription, or subscription after not being
                 * subscribed
                 */
                Sub,

                /**
                 * Renewed subscription
                 */
                Resub,

                /**
                 * Subscription gifted to a user from another user
                 */
                Gifted,

                /**
                 * Subscriptions gifted to a channel's community from a user
                 */
                MysteryGift,
            } type = Type::Unknown;

            /**
             * This is the channel to which the user subscribed.
             */
            std::string channel;

            /**
             * This is the name of the user who subscribed.
             */
            std::string user;

            /**
             * This is the display name of the user who received the sub, if it
             * was gifted.
             */
            std::string recipientDisplayName;

            /**
             * This is the user name of the user who received the sub, if it
             * was gifted.
             */
            std::string recipientUserName;

            /**
             * This is the ID of the user who received the sub, if it was
             * gifted.
             */
            uintmax_t recipientId = 0;

            /**
             * If this is a mystery gift announcement, this is the number of
             * subs that are being gifted to the community.
             */
            size_t massGiftCount = 0;

            /**
             * If this is a gifted sub, this is the number of gifted subs the
             * gifter has given so far in this channel.
             */
            size_t senderCount = 0;

            /**
             * This is the content of any message the user included when they
             * subscribed.
             */
            std::string userMessage;

            /**
             * This is the content of any message the system provided when the
             * user subscribed.
             */
            std::string systemMessage;

            /**
             * This is the name of the subscription plan chosen by the user.
             */
            std::string planName;

            /**
             * If this is a sub renewal, this is the number of consecutive
             * months the user has been subscribed to the channel.
             */
            size_t months = 0;

            /**
             * This is the numerical identifier of the subscription plan chosen
             * by the user.
             */
            uintmax_t planId = 0;

            /**
             * This contains information provided in the message's tags.
             */
            TagsInfo tags;
        };

        /**
         * This contains all the information about an incoming raid
         * notification.
         */
        struct RaidInfo {
            /**
             * This is the channel being raided.
             */
            std::string channel;

            /**
             * This is the name of the user/channel who raided.
             */
            std::string raider;

            /**
             * This is the number of new viewers who are raiding the channel.
             */
            size_t viewers = 0;

            /**
             * This is the content of any message the system provided when the
             * user subscribed.
             */
            std::string systemMessage;

            /**
             * This contains information provided in the message's tags.
             */
            TagsInfo tags;
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
             * This is called to notify the user that the Twitch server is
             * about to go down, and the user should expect a disconnection,
             * and might want to try reconnecting again after a short period of
             * time.
             */
            virtual void Doom() {
            }

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
             * This is called whenever the user receives a message sent
             * privately from another user.  This is generally only seen when
             * the special user "jtv" sends you a message to let you know when
             * someone else is hosting you.
             *
             * @param[in] messageInfo
             *     This contains all the information about the received
             *     message.
             */
            virtual void PrivateMessage(MessageInfo&& messageInfo) {
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
             * @param[in] noticeInfo
             *     This contains all the information about the received
             *     server notice.
             */
            virtual void Notice(NoticeInfo&& noticeInfo) {
            }

            /**
             * This is called whenever a hosting notification is received from
             * the server.
             *
             * @param[in] hostInfo
             *     This contains all the information about the received
             *     hosting notification.
             */
            virtual void Host(HostInfo&& hostInfo) {
            }

            /**
             * This is called whenever a notification about a room mode being
             * changed is received from the server.
             *
             * @param[in] roomModeChangeInfo
             *     This contains all the information about the received
             *     room mode change notification.
             */
            virtual void RoomModeChange(RoomModeChangeInfo&& roomModeChangeInfo) {
            }

            /**
             * This is called whenever a notification about chat being cleared,
             * a user being banned, or a user being timed out is received from
             * the server.
             *
             * @param[in] clearInfo
             *     This contains all the information about the received
             *     clear notification.
             */
            virtual void Clear(ClearInfo&& clearInfo) {
            }

            /**
             * This is called whenever the server announces that a user is or
             * is not a moderator.
             *
             * @param[in] modInfo
             *     This contains all the information about the received
             *     mod notification.
             */
            virtual void Mod(ModInfo&& modInfo) {
            }

            /**
             * This is called whenever the server provides the user with their
             * state, either globally, or within the context of a channel.
             *
             * @param[in] userStateInfo
             *     This contains all the information about the received
             *     user state notification.
             */
            virtual void UserState(UserStateInfo&& userStateInfo) {
            }

            /**
             * This is called whenever the server makes a subscription
             * announcement in a channel.
             *
             * @param[in] subInfo
             *     This contains all the information about the subscription
             *     announcement.
             */
            virtual void Sub(SubInfo&& subInfo) {
            }

            /**
             * This is called whenever the server announces that raid is coming
             * into a channel.
             *
             * @param[in] raidInfo
             *     This contains all the information about the incoming raid.
             */
            virtual void Raid(RaidInfo&& raidInfo) {
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
