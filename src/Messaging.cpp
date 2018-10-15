/**
 * @file Messaging.cpp
 *
 * This module contains the implementation of the
 * Twitch::Messaging class.
 *
 * © 2016-2018 by Richard Walters
 */

#include "Message.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <list>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <SystemAbstractions/StringExtensions.hpp>
#include <thread>
#include <Twitch/Messaging.hpp>
#include <vector>

namespace {

    /**
     * This is the required line terminator for lines of text
     * sent to or from Twitch chat servers.
     */
    const std::string CRLF = "\r\n";

    /**
     * This is the maximum amount of time to wait for the Twitch server to
     * provide the Message Of The Day (MOTD), confirming a successful log-in,
     * before timing out.
     */
    constexpr double LOG_IN_TIMEOUT_SECONDS = 5.0;

    /**
     * This is used to convey an action for the Messaging class worker
     * to either perform or await, including any necessary context.
     */
    struct Action {
        // Types

        /**
         * These are the types of actions which the Messaging class worker can
         * perform.
         */
        enum class Type {
            /**
             * Establish a new connection to Twitch chat, and use the new
             * connection to log in.
             */
            LogIn,

            /**
             * Request the IRCv3 capabilities of the server.
             */
            RequestCaps,

            /**
             * Wait for the message of the day (MOTD) from the server.
             */
            AwaitMotd,

            /**
             * Log out of Twitch chat, and close the active connection.
             */
            LogOut,

            /**
             * Process all messages received from the Twitch server.
             */
            ProcessMessagesReceived,

            /**
             * Handle when the server closes its end of the connection.
             */
            ServerDisconnected,

            /**
             * Join a chat room.
             */
            Join,

            /**
             * Leave a chat room.
             */
            Leave,

            /**
             * Send a message to a channel.
             */
            SendMessage,

            /**
             * Send a whisper to a channel.
             */
            SendWhisper,
        };

        // Properties

        /**
         * This is the type of action to perform.
         */
        Type type;

        /**
         * This is used with multiple actions, to provide the client
         * with the primary nickname associated with the command.
         */
        std::string nickname;

        /**
         * This is used with the LogIn action, to provide the client
         * with the OAuth token to be used to authenticate with the server.
         */
        std::string token;

        /**
         * This is used with multiple actions, to provide the client
         * with some context or text to be sent to the server.
         */
        std::string message;

        /**
         * This is the time, according to the time keeper, at which
         * the action will be considered timed out.
         */
        double expiration = 0.0;
    };

    /**
     * This method returns the nickname portion of a message prefix.
     *
     * @param[in] prefix
     *     This is the message prefix from which to extract the nickname.
     *
     * @return
     *     The nickname portion of the prefix is returned.
     */
    std::string ExtractNicknameFromPrefix(const std::string& prefix) {
        const auto nicknameDelimiter = prefix.find('!');
        if (nicknameDelimiter == std::string::npos) {
            return "";
        }
        return prefix.substr(0, nicknameDelimiter);
    }

}

namespace Twitch {

    /**
     * This contains the private properties of a Messaging instance.
     */
    struct Messaging::Impl {
        // Types

        /**
         * This is the type of member function pointer used to map
         * action types to their performer methods.
         *
         * @param[in] action
         *     This is the action to perform.
         */
        typedef void(Impl::*ActionPerformer)(Action&& action);

        /**
         * This is the type of member function pointer used to map
         * action types to their timeout methods.
         *
         * @param[in] action
         *     This is the action to time out.
         */
        typedef void(Impl::*ActionTimeout)(Action&& action);

        /**
         * This is the type of member function pointer used to map
         * server commands to their handlers.
         *
         * @param[in] message
         *     This holds information about the server command to handle.
         */
        typedef void(Impl::*ServerCommandHandler)(Message&& message);

        /**
         * This is the type of member function pointer used to map
         * action types to their message processors.
         *
         * @param[in,out] action
         *     This is the action for which to process the given message.
         *
         * @param[in] message
         *     This holds information about the message to process
         *     within the context of the given action.
         *
         * @return
         *     An indication of whether or not the action was completed by
         *     processing the given message is returned.
         */
        typedef bool(Impl::*ActionProcessor)(
            Action& action,
            const Message& message
        );

        /**
         * This is the type of map used to map action types to their message
         * processors.
         */
        typedef std::map< Action::Type, ActionProcessor > ActionProcessors;

        // Properties

        /**
         * This is used to synchronize access to the object.
         */
        std::mutex mutex;

        // --------------------------------------------------------------------
        // All properties in this section are protected by the mutex.
        // ⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇
        // --------------------------------------------------------------------

        /**
         * This is a helper object used to generate and publish
         * diagnostic messages.
         */
        SystemAbstractions::DiagnosticsSender diagnosticsSender;

        /**
         * This is the function to call in order to make a new
         * connection to the Twitch server.
         */
        ConnectionFactory connectionFactory;

        /**
         * This is the object to use to measure elapsed time periods.
         */
        std::shared_ptr< TimeKeeper > timeKeeper;

        /**
         * This is the object provided by the user of this class, in order to
         * receive notifications, events, and other callbacks from the class.
         */
        std::shared_ptr< User > user = std::make_shared< User >();

        /**
         * This is used to signal the worker thread to wake up.
         */
        std::condition_variable wakeWorker;

        /**
         * This flag indicates whether or not the worker thread
         * should be stopped.
         */
        bool stopWorker = false;

        /**
         * These are the actions to be performed by the worker thread.
         */
        std::deque< Action > actionsToBePerformed;

        /**
         * This is used to perform background tasks for the object.
         */
        std::thread worker;

        // --------------------------------------------------------------------
        // ⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆
        // All properties in this section are protected by the mutex.
        // --------------------------------------------------------------------

        // --------------------------------------------------------------------
        // All properties in this section should only be used by the worker
        // thread.
        // ⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇⬇
        // --------------------------------------------------------------------

        // This is the interface to the current connection to the Twitch
        // server, if we are connected.
        std::shared_ptr< Connection > connection;

        // This is essentially just a buffer to receive raw characters from
        // the Twitch server, until a complete line has been received,
        // removed from this buffer, and handled appropriately.
        std::string dataReceived;

        // This flag indicates whether or not the client has finished
        // logging into the Twitch server (we've received the Message Of
        // The Day (MOTD) from the server).
        bool loggedIn = false;

        // This holds onto any actions for which the worker is awaiting a
        // response from the server.
        std::list< Action > actionsAwaitingResponses;

        // These are the IRCv3 capabilities advertised by the server.
        std::set< std::string > capsSupported;

        // --------------------------------------------------------------------
        // ⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆⬆
        // All properties in this section should only be used by the worker
        // thread.
        // --------------------------------------------------------------------

        // Methods

        /**
         * This is the constructor for the structure.
         */
        Impl()
            : diagnosticsSender("TMI")
        {
        }

        /**
         * This method is called to send a raw line of text
         * to the Twitch server.  Do not include the line terminator (CRLF),
         * as this method adds one to the end when sending the line.
         *
         * @param[in,out] connection
         *     This is the connection to use to send the message.
         *
         * @param[in] rawLine
         *     This is the raw line of text to send to the Twitch server.
         *     Do not include the line terminator (CRLF),
         *     as this method adds one to the end when sending the line.
         */
        void SendLineToTwitchServer(
            Connection& connection,
            const std::string& rawLine
        ) {
            if (rawLine.substr(0, 11) == "PASS oauth:") {
                diagnosticsSender.SendDiagnosticInformationString(0, "< PASS oauth:**********************");
            } else {
                diagnosticsSender.SendDiagnosticInformationString(0, "< " + rawLine);
            }
            connection.Send(rawLine + CRLF);
        }

        /**
         * This method is called whenever any message is received from the
         * Twitch server for the user agent.
         *
         * @param[in] rawText
         *     This is the raw text received from the Twitch server.
         */
        void OnMessageReceived(const std::string& rawText) {
            std::lock_guard< decltype(mutex) > lock(mutex);
            Action action;
            action.type = Action::Type::ProcessMessagesReceived;
            action.message = rawText;
            actionsToBePerformed.push_back(action);
            wakeWorker.notify_one();
        }

        /**
         * This method is called when the Twitch server closes its end of the
         * connection.
         */
        void OnServerDisconnected() {
            std::lock_guard< decltype(mutex) > lock(mutex);
            Action action;
            action.type = Action::Type::ServerDisconnected;
            actionsToBePerformed.push_back(action);
            wakeWorker.notify_one();
        }

        /**
         * This method is called to request additional IRC capabilities for the
         * connection with the Twitch chat server.
         *
         * @param[in] nickname
         *     This is the nickname with which to log into Twitch chat.
         *
         * @param[in] token
         *     This is the OAuth token to use to authenticate with Twitch chat.
         */
        void RequestCapabilities(
            const std::string& nickname,
            const std::string& token
        ) {
            SendLineToTwitchServer(*connection, "CAP REQ :twitch.tv/commands");
            Action action;
            action.type = Action::Type::RequestCaps;
            action.nickname = nickname;
            action.token = token;
            if (timeKeeper != nullptr) {
                action.expiration = timeKeeper->GetCurrentTime() + LOG_IN_TIMEOUT_SECONDS;
            }
            actionsAwaitingResponses.push_back(std::move(action));
        }

        /**
         * This method is called to finish the capabilities negotiation phase
         * of logging into Twitch chat, send the user's authentication
         * information, and begin waiting for the Message of the Day (MOTD)
         * from the server, to indicate that we're logged in successfully.
         *
         * @param[in] nickname
         *     This is the nickname with which to log into Twitch chat.
         *
         * @param[in] token
         *     This is the OAuth token to use to authenticate with Twitch chat.
         */
        void EndCapabilitiesHandshakeAndAuthenticate(
            const std::string& nickname,
            const std::string& token
        ) {
            SendLineToTwitchServer(*connection, "CAP END");
            SendLineToTwitchServer(*connection, "PASS oauth:" + token);
            SendLineToTwitchServer(*connection, "NICK " + nickname);
            Action action;
            action.type = Action::Type::AwaitMotd;
            action.nickname = nickname;
            action.token = token;
            if (timeKeeper != nullptr) {
                action.expiration = timeKeeper->GetCurrentTime() + LOG_IN_TIMEOUT_SECONDS;
            }
            actionsAwaitingResponses.push_back(std::move(action));
        }

        /**
         * This method is called whenever the user agent disconnects from the
         * Twitch server.
         *
         * @param[in] farewell
         *     If not empty, the user agent should sent a QUIT command before
         *     disconnecting, and this is a message to include in the
         *     QUIT command.
         */
        void Disconnect(const std::string farewell = "") {
            if (connection == nullptr) {
                return;
            }
            if (!farewell.empty()) {
                connection->Send("QUIT :" + farewell + CRLF);
            }
            connection->Disconnect();
            user->LogOut();
        }

        /**
         * This method is called to process the given message through all
         * actions awaiting responses, removing any actions that are completed
         * after processing the message.
         *
         * @param[in] message
         *     This is the message to process through all actions awaiting
         *     responses.
         *
         * @param[in] actionProcessors
         *     This is the map of action types to processing methods to use
         *     for the given message.
         */
        void ProcessMessageWithAwaitingActions(
            const Message& message,
            const ActionProcessors& actionProcessors
        ) {
            for (
                auto it = actionsAwaitingResponses.begin(),
                end = actionsAwaitingResponses.end();
                it != end;
            ) {
                auto& action = *it;
                const auto actionProcessor = actionProcessors.find(action.type);
                if (
                    (actionProcessor != actionProcessors.end())
                    && (this->*(actionProcessor->second))(action, message)
                ) {
                    it = actionsAwaitingResponses.erase(it);
                } else {
                    ++it;
                }
            }
        }

        /**
         * This method is called from the worker thread to time out any actions
         * that are awaiting responses but have expired.
         */
        void ProcessTimeouts() {
            const auto now = timeKeeper->GetCurrentTime();
            for (
                auto it = actionsAwaitingResponses.begin(),
                end = actionsAwaitingResponses.end();
                it != end;
            ) {
                auto& action = *it;
                if (now >= action.expiration) {
                    TimeoutAction(std::move(action));
                    it = actionsAwaitingResponses.erase(it);
                } else {
                    ++it;
                }
            }
        }

        /**
         * This method posts the given action to the queue of actions to be
         * performed by the worker thread, and wakes the worker thread up.
         *
         * @param[in] action
         *     This is the action to perform.
         */
        void PostAction(Action&& action) {
            std::lock_guard< decltype(mutex) > lock(mutex);
            actionsToBePerformed.push_back(std::move(action));
            wakeWorker.notify_one();
        }

        /**
         * This method performs the given action.
         *
         * @param[in] action
         *     This is the action to perform.
         */
        void PerformAction(Action&& action) {
            static const std::map< Action::Type, ActionPerformer > actionPerformers = {
                {Action::Type::LogIn, &Impl::PerformActionLogIn},
                {Action::Type::LogOut, &Impl::PerformActionLogOut},
                {Action::Type::ProcessMessagesReceived, &Impl::PerformActionProcessMessagesReceived},
                {Action::Type::ServerDisconnected, &Impl::PerformActionServerDisconnected},
                {Action::Type::Join, &Impl::PerformActionJoin},
                {Action::Type::Leave, &Impl::PerformActionLeave},
                {Action::Type::SendMessage, &Impl::PerformActionSendMessage},
                {Action::Type::SendWhisper, &Impl::PerformActionSendWhisper},
            };
            const auto actionPerformer = actionPerformers.find(action.type);
            if (actionPerformer != actionPerformers.end()) {
                (this->*(actionPerformer->second))(std::move(action));
            }
        }

        /**
         * This method times out the given action.
         *
         * @param[in] action
         *     This is the action to time out.
         */
        void TimeoutAction(Action&& action) {
            static const std::map< Action::Type, ActionTimeout > actionTimeouts = {
                {Action::Type::LogIn, &Impl::TimeoutActionLogIn},
                {Action::Type::RequestCaps, &Impl::TimeoutActionRequestCaps},
                {Action::Type::AwaitMotd, &Impl::TimeoutActionAwaitMotd},
            };
            const auto actionTimeout = actionTimeouts.find(action.type);
            if (actionTimeout != actionTimeouts.end()) {
                (this->*(actionTimeout->second))(std::move(action));
            }
        }

        /**
         * This method is used as an action processor when the action should
         * just be discarded without actually doing anything more.
         *
         * @param[in,out] action
         *     This is the action for which to process the given message.
         *
         * @param[in] message
         *     This holds information about the message to process
         *     within the context of the given action.
         *
         * @return
         *     An indication of whether or not the action was completed by
         *     processing the given message is returned.
         */
        bool DiscardAction(
            Action& action,
            const Message& message
        ) {
            return false;
        }

        /**
         * This method performs the given LogIn action.
         *
         * @param[in] action
         *     This is the action to perform.
         */
        void PerformActionLogIn(Action&& action) {
            if (connection != nullptr) {
                return;
            }
            connection = connectionFactory();
            connection->SetMessageReceivedDelegate(
                std::bind(&Impl::OnMessageReceived, this, std::placeholders::_1)
            );
            connection->SetDisconnectedDelegate(
                std::bind(&Impl::OnServerDisconnected, this)
            );
            if (connection->Connect()) {
                capsSupported.clear();
                SendLineToTwitchServer(*connection, "CAP LS 302");
                if (timeKeeper != nullptr) {
                    action.expiration = timeKeeper->GetCurrentTime() + LOG_IN_TIMEOUT_SECONDS;
                }
                actionsAwaitingResponses.push_back(std::move(action));
            } else {
                user->LogOut();
            }
        }

        /**
         * This method processes the given CAP message in context of the given
         * LogIn action.
         *
         * @param[in,out] action
         *     This is the action for which to process the given message.
         *
         * @param[in] message
         *     This holds information about the message to process
         *     within the context of the given action.
         *
         * @return
         *     An indication of whether or not the action was completed by
         *     processing the given message is returned.
         */
        bool ProcessActionLogInCap(
            Action& action,
            const Message& message
        ) {
            if (
                (message.parameters.size() < 3)
                || (message.parameters[1] != "LS")
            ) {
                return false;
            }
            if (message.parameters[2] == "*") {
                const auto newCapsSupported = SystemAbstractions::Split(message.parameters[3], ' ');
                capsSupported.insert(newCapsSupported.begin(), newCapsSupported.end());
                return false;
            } else {
                const auto newCapsSupported = SystemAbstractions::Split(message.parameters[2], ' ');
                capsSupported.insert(newCapsSupported.begin(), newCapsSupported.end());
                if (capsSupported.find("twitch.tv/commands") == capsSupported.end()) {
                    EndCapabilitiesHandshakeAndAuthenticate(
                        action.nickname,
                        action.token
                    );
                } else {
                    RequestCapabilities(
                        action.nickname,
                        action.token
                    );
                }
                return true;
            }
        }

        /**
         * This method times out the given LogIn action.
         *
         * @param[in] action
         *     This is the action to time out.
         */
        void TimeoutActionLogIn(Action&& action) {
            Disconnect("Timeout waiting for capability list");
        }

        /**
         * This method processes the given CAP message in context of the given
         * RequestCaps action.
         *
         * @param[in,out] action
         *     This is the action for which to process the given message.
         *
         * @param[in] message
         *     This holds information about the message to process
         *     within the context of the given action.
         *
         * @return
         *     An indication of whether or not the action was completed by
         *     processing the given message is returned.
         */
        bool ProcessActionRequestCapsCap(
            Action& action,
            const Message& message
        ) {
            if (
                (message.parameters.size() < 2)
                || (
                    (message.parameters[1] != "ACK")
                    && (message.parameters[1] != "NAK")
                )
            ) {
                return false;
            }
            EndCapabilitiesHandshakeAndAuthenticate(
                action.nickname,
                action.token
            );
            return true;
        }

        /**
         * This method times out the given RequestCaps action.
         *
         * @param[in] action
         *     This is the action to time out.
         */
        void TimeoutActionRequestCaps(Action&& action) {
            Disconnect("Timeout waiting for response to capability request");
        }

        /**
         * This method processes the given Message of the Day (MOTD) message in
         * context of the given AwaitMotd action.
         *
         * @param[in,out] action
         *     This is the action for which to process the given message.
         *
         * @param[in] message
         *     This holds information about the message to process
         *     within the context of the given action.
         *
         * @return
         *     An indication of whether or not the action was completed by
         *     processing the given message is returned.
         */
        bool ProcessActionAwaitMotdMotd(
            Action& action,
            const Message& message
        ) {
            if (!loggedIn) {
                loggedIn = true;
                user->LogIn();
            }
            return true;
        }

        /**
         * This method times out the given AwaitMotd action.
         *
         * @param[in] action
         *     This is the action to time out.
         */
        void TimeoutActionAwaitMotd(Action&& action) {
            Disconnect("Timeout waiting for MOTD");
        }

        /**
         * This method performs the given LogOut action.
         *
         * @param[in] action
         *     This is the action to perform.
         */
        void PerformActionLogOut(Action&& action) {
            Disconnect(action.message);
        }

        /**
         * This method performs the given ProcessMessagesReceived action.
         *
         * @param[in] action
         *     This is the action to perform.
         */
        void PerformActionProcessMessagesReceived(Action&& action) {
            static const std::map< std::string, ServerCommandHandler > serverCommandHandlers = {
                {"376", &Impl::HandleServerCommandMotd},
                {"PING", &Impl::HandleServerCommandPing},
                {"JOIN", &Impl::HandleServerCommandJoin},
                {"PART", &Impl::HandleServerCommandPart},
                {"PRIVMSG", &Impl::HandleServerCommandPrivMsg},
                {"CAP", &Impl::HandleServerCommandCap},
                {"WHISPER", &Impl::HandleServerCommandWhisper},
                {"NOTICE", &Impl::HandleServerCommandNotice},
            };
            dataReceived += action.message;
            Message message;
            while (Message::Parse(dataReceived, message, diagnosticsSender)) {
                const auto commandHandler = serverCommandHandlers.find(message.command);
                if (commandHandler != serverCommandHandlers.end()) {
                    (this->*(commandHandler->second))(std::move(message));
                }
            }
        }

        /**
         * This method is called to handle the end-of-MOTD command (376) from
         * the Twitch server.
         *
         * @param[in] message
         *     This holds information about the server command to handle.
         */
        void HandleServerCommandMotd(Message&& message) {
            static const ActionProcessors motdActionProcessors = {
                {Action::Type::AwaitMotd, &Impl::ProcessActionAwaitMotdMotd},
            };
            ProcessMessageWithAwaitingActions(
                message,
                motdActionProcessors
            );
        }

        /**
         * This method is called to handle the PING command from the Twitch
         * server.
         *
         * @param[in] message
         *     This holds information about the server command to handle.
         */
        void HandleServerCommandPing(Message&& message) {
            if (message.parameters.size() < 1) {
                return;
            }
            const auto server = message.parameters[0];
            if (connection != nullptr) {
                SendLineToTwitchServer(*connection, "PONG :" + server);
            }
        }

        /**
         * This method is called to handle the JOIN command from the Twitch
         * server.
         *
         * @param[in] message
         *     This holds information about the server command to handle.
         */
        void HandleServerCommandJoin(Message&& message) {
            if (
                (message.parameters.size() < 1)
                && (message.parameters[0].length() < 2)
            ) {
                return;
            }
            const auto nicknameDelimiter = message.prefix.find('!');
            if (nicknameDelimiter == std::string::npos) {
                return;
            }
            MembershipInfo membershipInfo;
            membershipInfo.user = message.prefix.substr(0, nicknameDelimiter);
            membershipInfo.channel = message.parameters[0].substr(1);
            user->Join(std::move(membershipInfo));
        }

        /**
         * This method is called to handle the PART command from the Twitch
         * server.
         *
         * @param[in] message
         *     This holds information about the server command to handle.
         */
        void HandleServerCommandPart(Message&& message) {
            if (
                (message.parameters.size() < 1)
                && (message.parameters[0].length() < 2)
            ) {
                return;
            }
            const auto nicknameDelimiter = message.prefix.find('!');
            if (nicknameDelimiter == std::string::npos) {
                return;
            }
            MembershipInfo membershipInfo;
            membershipInfo.user = message.prefix.substr(0, nicknameDelimiter);
            membershipInfo.channel = message.parameters[0].substr(1);
            user->Leave(std::move(membershipInfo));
        }

        /**
         * This method is called to handle the PRIVMSG command from the Twitch
         * server.
         *
         * @param[in] message
         *     This holds information about the server command to handle.
         */
        void HandleServerCommandPrivMsg(Message&& message) {
            if (
                (message.parameters.size() < 2)
                && (message.parameters[0].length() < 2)
            ) {
                return;
            }
            const auto nickname = ExtractNicknameFromPrefix(message.prefix);
            MessageInfo messageInfo;
            messageInfo.user = nickname;
            messageInfo.channel = message.parameters[0].substr(1);
            messageInfo.message = message.parameters[1];
            user->Message(std::move(messageInfo));
        }

        /**
         * This method is called to handle the CAP command from the Twitch
         * server.
         *
         * @param[in] message
         *     This holds information about the server command to handle.
         */
        void HandleServerCommandCap(Message&& message) {
            static const ActionProcessors capActionProcessors = {
                {Action::Type::LogIn, &Impl::ProcessActionLogInCap},
                {Action::Type::RequestCaps, &Impl::ProcessActionRequestCapsCap},
            };
            ProcessMessageWithAwaitingActions(
                message,
                capActionProcessors
            );
        }

        /**
         * This method is called to handle the WHISPER command from the Twitch
         * server.
         *
         * @param[in] message
         *     This holds information about the server command to handle.
         */
        void HandleServerCommandWhisper(Message&& message) {
            if (
                (message.parameters.size() < 2)
                && (message.parameters[0].length() < 1)
            ) {
                return;
            }
            const auto nickname = ExtractNicknameFromPrefix(message.prefix);
            WhisperInfo whisperInfo;
            whisperInfo.user = nickname;
            whisperInfo.message = message.parameters[1];
            user->Whisper(std::move(whisperInfo));
        }

        /**
         * This method is called to handle the NOTICE command from the Twitch
         * server.
         *
         * @param[in] message
         *     This holds information about the server command to handle.
         */
        void HandleServerCommandNotice(Message&& message) {
            if (
                (message.parameters.size() < 2)
                && (message.parameters[0].length() < 1)
            ) {
                return;
            }
            const auto& noticeText = message.parameters[1];
            user->Notice(noticeText);
            if (
                !loggedIn
                && (noticeText == "Login unsuccessful")
            ) {
                user->LogOut();
                static const ActionProcessors loginFailActionProcessors = {
                    {Action::Type::AwaitMotd, &Impl::DiscardAction},
                };
                ProcessMessageWithAwaitingActions(
                    message,
                    loginFailActionProcessors
                );
            }
        }

        /**
         * This method performs the given ServerDisconnected action.
         *
         * @param[in] action
         *     This is the action to perform.
         */
        void PerformActionServerDisconnected(Action&& action) {
            Disconnect();
        }

        /**
         * This method performs the given Join action.
         *
         * @param[in] action
         *     This is the action to perform.
         */
        void PerformActionJoin(Action&& action) {
            if (connection == nullptr) {
                return;
            }
            SendLineToTwitchServer(*connection, "JOIN #" + action.nickname);
        }

        /**
         * This method performs the given Leave action.
         *
         * @param[in] action
         *     This is the action to perform.
         */
        void PerformActionLeave(Action&& action) {
            if (connection == nullptr) {
                return;
            }
            SendLineToTwitchServer(*connection, "PART #" + action.nickname);
        }

        /**
         * This method performs the given SendMessage action.
         *
         * @param[in] action
         *     This is the action to perform.
         */
        void PerformActionSendMessage(Action&& action) {
            if (connection == nullptr) {
                return;
            }
            SendLineToTwitchServer(*connection, "PRIVMSG #" + action.nickname + " :" + action.message);
        }

        /**
         * This method performs the given SendWhisper action.
         *
         * @param[in] action
         *     This is the action to perform.
         */
        void PerformActionSendWhisper(Action&& action) {
            if (connection == nullptr) {
                return;
            }
            SendLineToTwitchServer(*connection, "PRIVMSG #jtv :.w " + action.nickname + " " + action.message);
        }

        /**
         * This method signals the worker thread to stop.
         */
        void StopWorker() {
            std::lock_guard< decltype(mutex) > lock(mutex);
            stopWorker = true;
            wakeWorker.notify_one();
        }

        /**
         * This runs in its own thread and performs background tasks
         * for the object.
         */
        void Worker() {
            std::unique_lock< decltype(mutex) > lock(mutex);
            while (!stopWorker) {
                lock.unlock();
                if (timeKeeper != nullptr) {
                    ProcessTimeouts();
                }
                lock.lock();
                while (!actionsToBePerformed.empty()) {
                    auto action = actionsToBePerformed.front();
                    actionsToBePerformed.pop_front();
                    lock.unlock();
                    PerformAction(std::move(action));
                    lock.lock();
                }
                if (!actionsAwaitingResponses.empty()) {
                    wakeWorker.wait_for(
                        lock,
                        std::chrono::milliseconds(50),
                        [this]{
                            return (
                                stopWorker
                                || !actionsToBePerformed.empty()
                            );
                        }
                    );
                } else {
                    wakeWorker.wait(
                        lock,
                        [this]{
                            return (
                                stopWorker
                                || !actionsToBePerformed.empty()
                            );
                        }
                    );
                }
            }
        }
    };

    Messaging::~Messaging() noexcept {
        impl_->StopWorker();
        impl_->worker.join();
    }

    Messaging::Messaging()
        : impl_ (new Impl())
    {
        impl_->worker = std::thread(&Impl::Worker, impl_.get());
    }

    SystemAbstractions::DiagnosticsSender::UnsubscribeDelegate Messaging::SubscribeToDiagnostics(
        SystemAbstractions::DiagnosticsSender::DiagnosticMessageDelegate delegate,
        size_t minLevel
    ) {
        return impl_->diagnosticsSender.SubscribeToDiagnostics(delegate, minLevel);
    }

    void Messaging::SetConnectionFactory(ConnectionFactory connectionFactory) {
        impl_->connectionFactory = connectionFactory;
    }

    void Messaging::SetTimeKeeper(std::shared_ptr< TimeKeeper > timeKeeper) {
        impl_->timeKeeper = timeKeeper;
    }

    void Messaging::SetUser(std::shared_ptr< User > user) {
        impl_->user = user;
    }

    void Messaging::LogIn(
        const std::string& nickname,
        const std::string& token
    ) {
        Action action;
        action.type = Action::Type::LogIn;
        action.nickname = nickname;
        action.token = token;
        impl_->PostAction(std::move(action));
    }

    void Messaging::LogOut(const std::string& farewell) {
        Action action;
        action.type = Action::Type::LogOut;
        action.message = farewell;
        impl_->PostAction(std::move(action));
    }

    void Messaging::Join(const std::string& channel) {
        Action action;
        action.type = Action::Type::Join;
        action.nickname = channel;
        impl_->PostAction(std::move(action));
    }

    void Messaging::Leave(const std::string& channel) {
        Action action;
        action.type = Action::Type::Leave;
        action.nickname = channel;
        impl_->PostAction(std::move(action));
    }

    void Messaging::SendMessage(
        const std::string& channel,
        const std::string& message
    ) {
        Action action;
        action.type = Action::Type::SendMessage;
        action.nickname = channel;
        action.message = message;
        impl_->PostAction(std::move(action));
    }

    void Messaging::SendWhisper(
        const std::string& nickname,
        const std::string& message
    ) {
        Action action;
        action.type = Action::Type::SendWhisper;
        action.nickname = nickname;
        action.message = message;
        impl_->PostAction(std::move(action));
    }

}
