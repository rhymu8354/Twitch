/**
 * @file Messaging.cpp
 *
 * This module contains the implementation of the
 * Twitch::Messaging class.
 *
 * © 2016-2018 by Richard Walters
 */

#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <queue>
#include <string>
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
     * These are the states in which the Messaging class can be.
     */
    enum class State {
        /**
         * The client has either not yet logged in, or has logged out.
         * There is no active connection.
         */
        NotLoggedIn,

        /**
         * The client has completely logged into the server,
         * with an active connection.
         */
        LoggedIn,
    };

    /**
     * These are the types of actions which the Messaging class worker can
     * perform.
     */
    enum class ActionType {
        /**
         * Establish a new connection to Twitch chat, and use the new
         * connection to log in.
         */
        LogIn,

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
    };

    /**
     * This is used to convey an action for the Messaging class worker
     * to perform, including any parameters.
     */
    struct Action {
        /**
         * This is the type of action to perform.
         */
        ActionType type;

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
    };

    /**
     * This contains all the information parsed from a single message
     * from the Twitch server.
     */
    struct Message {
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
    };

    /**
     * This represents a condition the worker is awaiting, which
     * might time out.
     */
    struct TimeoutCondition {
        // Properties

        /**
         * This is the type of action which prompted the wait condition.
         */
        ActionType type;

        /**
         * This is the time, according to the time keeper, at which
         * the condition will be considered as having timed out.
         */
        double expiration = 0.0;

        // Methods

        /**
         * This method is used to sort timeout conditions by expiration time.
         *
         * @param[in] rhs
         *     This is the other timeout condition to compare with this one.
         *
         * @return
         *     This returns true if the other timeout condition will expire
         *     first.
         */
        bool operator<(const TimeoutCondition& rhs) const {
            return expiration > rhs.expiration;
        }
    };

}

namespace Twitch {

    /**
     * This contains the private properties of a Messaging instance.
     */
    struct Messaging::Impl {
        // Properties

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
         * This is used to synchronize access to the object.
         */
        std::mutex mutex;

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
         * This is used to perform background tasks for the object.
         */
        std::thread worker;

        /**
         * These are the actions to be performed by the worker thread.
         */
        std::deque< Action > actions;

        // Methods

        /**
         * This method extracts the next message received from the
         * Twitch server.
         *
         * @param[in,out] dataReceived
         *      This is essentially just a buffer to receive raw characters
         *      from the Twitch server, until a complete line has been
         *      received, removed from this buffer, and handled appropriately.
         *
         * @param[out] message
         *     This is where to store the next message received from the
         *     Twitch server.
         *
         * @return
         *     An indication of whether or not a complete line was
         *     extracted is returned.
         */
        bool GetNextMessage(
            std::string& dataReceived,
            Message& message
        ) {
            // Extract the next line.
            const auto lineEnd = dataReceived.find(CRLF);
            if (lineEnd == std::string::npos) {
                return false;
            }
            const auto line = dataReceived.substr(0, lineEnd);

            // Remove the line from the buffer.
            dataReceived = dataReceived.substr(lineEnd + CRLF.length());

            // Unpack the message from the line.
            size_t offset = 0;
            int state = 0;
            message = Message();
            while (offset < line.length()) {
                switch (state) {
                    // First character of the line.  It could be ':',
                    // which signals a prefix, or it's the first character
                    // of the command.
                    case 0: {
                        if (line[offset] == ':') {
                            state = 1;
                        } else {
                        }
                    } break;

                    // Prefix
                    case 1: {
                        if (line[offset] == ' ') {
                            state = 2;
                        } else {
                            message.prefix += line[offset];
                        }
                    } break;

                    // First character of command
                    case 2: {
                        if (line[offset] != ' ') {
                            state = 3;
                            message.command += line[offset];
                        }
                    } break;

                    // Command
                    case 3: {
                        if (line[offset] == ' ') {
                            state = 4;
                        } else {
                            message.command += line[offset];
                        }
                    } break;

                    // First character of parameter
                    case 4: {
                        if (line[offset] == ':') {
                            state = 6;
                            message.parameters.push_back("");
                        } else if (line[offset] != ' ') {
                            state = 5;
                            message.parameters.push_back(line.substr(offset, 1));
                        }
                    } break;

                    // Parameter (not last, or last having no spaces)
                    case 5: {
                        if (line[offset] == ' ') {
                            state = 4;
                        } else {
                            message.parameters.back() += line[offset];
                        }
                    } break;

                    // Last Parameter (may include spaces)
                    case 6: {
                        message.parameters.back() += line[offset];
                    } break;
                }
                ++offset;
            }
            if (
                (state == 0)
                || (state == 1)
                || (state == 2)
            ) {
                message.command.clear();
            }
            return true;
        }

        /**
         * This method is called to whenever any message is received from the
         * Twitch server for the user agent.
         *
         * @param[in] rawText
         *     This is the raw text received from the Twitch server.
         */
        void MessageReceived(const std::string& rawText) {
            std::lock_guard< decltype(impl_->mutex) > lock(mutex);
            Action action;
            action.type = ActionType::ProcessMessagesReceived;
            action.message = rawText;
            actions.push_back(action);
            wakeWorker.notify_one();
        }

        /**
         * This method is called when the Twitch server closes its end of the
         * connection.
         */
        void ServerDisconnected() {
            std::lock_guard< decltype(impl_->mutex) > lock(mutex);
            Action action;
            action.type = ActionType::ServerDisconnected;
            actions.push_back(action);
            wakeWorker.notify_one();
        }

        /**
         * This method is called whenever the user agent disconnects from the
         * Twitch server.
         *
         * @param[in,out] connection
         *     This is the connection to disconnect.
         *
         * @param[in] farewell
         *     If not empty, the user agent should sent a QUIT command before
         *     disconnecting, and this is a message to include in the
         *     QUIT command.
         */
        void Disconnect(
            Connection& connection,
            const std::string farewell = ""
        ) {
            if (!farewell.empty()) {
                connection.Send("QUIT :" + farewell + CRLF);
            }
            connection.Disconnect();
            user->LogOut();
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
            // This is the interface to the current connection to the Twitch
            // server, if we are connected.
            std::shared_ptr< Connection > connection;

            // This is essentially just a buffer to receive raw characters from
            // the Twitch server, until a complete line has been received, removed
            // from this buffer, and handled appropriately.
            std::string dataReceived;

            // This flag indicates whether or not the client has finished logging
            // into the Twitch server (we've received the Message Of The Day
            // (MOTD) from the server).
            bool loggedIn = false;

            // This holds onto any conditions the worker is awaiting, which
            // might time out.
            std::priority_queue< TimeoutCondition > timeoutConditions;

            std::unique_lock< decltype(mutex) > lock(mutex);
            while (!stopWorker) {
                lock.unlock();
                if (!timeoutConditions.empty()) {
                    const auto timeoutCondition = timeoutConditions.top();
                    if (timeKeeper->GetCurrentTime() >= timeoutCondition.expiration) {
                        switch (timeoutCondition.type) {
                            case ActionType::LogIn: {
                                Disconnect(*connection, "Timeout waiting for MOTD");
                            }

                            default: {
                            } break;
                        }
                        timeoutConditions.pop();
                    }
                }
                lock.lock();
                while (!actions.empty()) {
                    const auto nextAction = actions.front();
                    actions.pop_front();
                    lock.unlock();
                    switch (nextAction.type) {
                        case ActionType::LogIn: {
                            if (connection != nullptr) {
                                break;
                            }
                            connection = connectionFactory();
                            connection->SetMessageReceivedDelegate(
                                std::bind(&Impl::MessageReceived, this, std::placeholders::_1)
                            );
                            connection->SetDisconnectedDelegate(
                                std::bind(&Impl::ServerDisconnected, this)
                            );
                            if (connection->Connect()) {
                                connection->Send("PASS oauth:" + nextAction.token + CRLF);
                                connection->Send("NICK " + nextAction.nickname + CRLF);
                                if (timeKeeper != nullptr) {
                                    TimeoutCondition timeoutCondition;
                                    timeoutCondition.type = ActionType::LogIn;
                                    timeoutCondition.expiration = timeKeeper->GetCurrentTime() + LOG_IN_TIMEOUT_SECONDS;
                                    timeoutConditions.push(timeoutCondition);
                                }
                            } else {
                                user->LogOut();
                            }
                        } break;

                        case ActionType::LogOut: {
                            if (connection != nullptr) {
                                Disconnect(*connection, nextAction.message);
                            }
                        } break;

                        case ActionType::ProcessMessagesReceived: {
                            dataReceived += nextAction.message;
                            Message message;
                            while (GetNextMessage(dataReceived, message)) {
                                if (message.command.empty()) {
                                    continue;
                                }
                                if (message.command == "376") { // RPL_ENDOFMOTD (RFC 1459)
                                    if (!loggedIn) {
                                        loggedIn = true;
                                        user->LogIn();
                                    }
                                } else if (message.command == "JOIN") {
                                    if (
                                        (message.parameters.size() < 1)
                                        && (message.parameters[0].length() < 2)
                                    ) {
                                        continue;
                                    }
                                    const auto nicknameDelimiter = message.prefix.find('!');
                                    if (nicknameDelimiter == std::string::npos) {
                                        continue;
                                    }
                                    const auto nickname = message.prefix.substr(0, nicknameDelimiter);
                                    const auto channel = message.parameters[0].substr(1);
                                    user->Join(channel, nickname);
                                }
                            }
                        } break;

                        case ActionType::ServerDisconnected: {
                            Disconnect(*connection);
                        } break;

                        case ActionType::Join: {
                            if (connection == nullptr) {
                                break;
                            }
                            connection->Send("JOIN #" + nextAction.nickname + CRLF);
                        } break;

                        default: {
                        } break;
                    }
                    lock.lock();
                }
                if (!timeoutConditions.empty()) {
                    wakeWorker.wait_for(
                        lock,
                        std::chrono::milliseconds(50),
                        [this]{
                            return (
                                stopWorker
                                || !actions.empty()
                            );
                        }
                    );
                } else {
                    wakeWorker.wait(
                        lock,
                        [this]{
                            return (
                                stopWorker
                                || !actions.empty()
                            );
                        }
                    );
                }
            }
        }
    };

    Messaging::~Messaging() {
        impl_->StopWorker();
        impl_->worker.join();
    }

    Messaging::Messaging()
        : impl_ (new Impl())
    {
        impl_->worker = std::thread(&Impl::Worker, impl_.get());
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
        std::lock_guard< decltype(impl_->mutex) > lock(impl_->mutex);
        Action action;
        action.type = ActionType::LogIn;
        action.nickname = nickname;
        action.token = token;
        impl_->actions.push_back(action);
        impl_->wakeWorker.notify_one();
    }

    void Messaging::LogOut(const std::string& farewell) {
        std::lock_guard< decltype(impl_->mutex) > lock(impl_->mutex);
        Action action;
        action.type = ActionType::LogOut;
        action.message = farewell;
        impl_->actions.push_back(action);
        impl_->wakeWorker.notify_one();
    }

    void Messaging::Join(const std::string& channel) {
        std::lock_guard< decltype(impl_->mutex) > lock(impl_->mutex);
        Action action;
        action.type = ActionType::Join;
        action.nickname = channel;
        impl_->actions.push_back(action);
        impl_->wakeWorker.notify_one();
    }

}
