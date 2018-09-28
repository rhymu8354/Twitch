/**
 * @file Messaging.cpp
 *
 * This module contains the implementation of the
 * Twitch::Messaging class.
 *
 * © 2016-2018 by Richard Walters
 */

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <Twitch/Messaging.hpp>

namespace {

    /**
     * This is the required line terminator for lines of text
     * sent to or from Twitch chat servers.
     */
    const std::string CRLF = "\r\n";

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
         * This is used with the LogIn action, to provide the client
         * with the nickname to be used in the chat session.
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
         * This is the function to call when the user agent successfully logs
         * into the Twitch server.
         */
        LoggedInDelegate loggedInDelegate;

        /**
         * This is the function to call when the user agent completes
         * logging out of the Twitch server.
         */
        LoggedOutDelegate loggedOutDelegate;

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

        /**
         * This is the interface to the current connection to the Twitch
         * server, if we are connected.
         */
        std::shared_ptr< Connection > connection;

        // Methods

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
                while (!actions.empty()) {
                    const auto nextAction = actions.front();
                    actions.pop_front();
                    switch (nextAction.type) {
                        case ActionType::LogIn: {
                            if (connection != nullptr) {
                                break;
                            }
                            connection = connectionFactory();
                            if (connection->Connect()) {
                                connection->Send("PASS oauth:" + nextAction.token + CRLF);
                                connection->Send("NICK " + nextAction.nickname + CRLF);
                                if (loggedInDelegate != nullptr) {
                                    loggedInDelegate();
                                }
                            } else {
                                if (loggedOutDelegate != nullptr) {
                                    loggedOutDelegate();
                                }
                            }
                        } break;

                        case ActionType::LogOut: {
                            if (connection != nullptr) {
                                connection->Send("QUIT :" + nextAction.message + CRLF);
                                connection->Disconnect();
                                if (loggedOutDelegate != nullptr) {
                                    loggedOutDelegate();
                                }
                            }
                        } break;

                        default: {
                        } break;
                    }
                }
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

    void Messaging::SetLoggedInDelegate(LoggedInDelegate loggedInDelegate) {
        impl_->loggedInDelegate = loggedInDelegate;
    }

    void Messaging::SetLoggedOutDelegate(LoggedOutDelegate loggedOutDelegate) {
        impl_->loggedOutDelegate = loggedOutDelegate;
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

}

