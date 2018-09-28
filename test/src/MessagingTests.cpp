/**
 * @file MessagingTests.cpp
 *
 * This module contains the unit tests of the Twitch::Messaging class.
 *
 * © 2018 by Richard Walters
 */

#include <algorithm>
#include <condition_variable>
#include <gtest/gtest.h>
#include <memory>
#include <mutex>
#include <string>
#include <SystemAbstractions/StringExtensions.hpp>
#include <Twitch/Connection.hpp>
#include <Twitch/Messaging.hpp>
#include <Twitch/TimeKeeper.hpp>
#include <vector>

namespace {

    /**
     * This is the required line terminator for lines of text
     * sent to or from Twitch chat servers.
     */
    const std::string CRLF = "\r\n";

    /**
     * This is a fake Twitch server used to test the Messaging class.
     */
    struct MockServer
        : public Twitch::Connection
    {
        // Properties

        std::condition_variable wakeCondition;
        std::mutex mutex;
        MessageReceivedDelegate messageReceivedDelegate;
        DisconnectedDelegate disconnectedDelegate;
        bool failConnectionAttempt = false;
        bool isConnected = false;
        bool isDisconnected = false;
        bool connectionProblem = false;
        std::string dataReceived;
        std::string nicknameOffered;
        std::string passwordOffered;
        std::vector< std::string > linesReceived;

        // Methods

        bool AwaitNickname() {
            std::unique_lock< std::mutex > lock(mutex);
            return wakeCondition.wait_for(
                lock,
                std::chrono::milliseconds(100),
                [this]{ return !nicknameOffered.empty(); }
            );
        }

        bool AwaitLineReceived(const std::string& line) {
            std::unique_lock< std::mutex > lock(mutex);
            return wakeCondition.wait_for(
                lock,
                std::chrono::milliseconds(100),
                [this, line]{
                    return std::find(
                        linesReceived.begin(),
                        linesReceived.end(),
                        line
                    ) != linesReceived.end();
                }
            );
        }

        std::string GetNicknameOffered() {
            return nicknameOffered;
        }

        std::string GetPasswordOffered() {
            return passwordOffered;
        }

        std::vector< std::string > GetLinesReceived() {
            return linesReceived;
        }

        void ClearLinesReceived() {
            linesReceived.clear();
        }

        bool IsConnected() {
            return isConnected;
        }

        bool IsDisconnected() {
            return isDisconnected;
        }

        bool WasThereAConnectionProblem() {
            return connectionProblem;
        }

        void FailConnectionAttempt() {
            failConnectionAttempt = true;
        }

        void ReturnToClient(const std::string& message) {
            if (messageReceivedDelegate != nullptr) {
                messageReceivedDelegate(message);
            }
        }

        void DisconnectClient() {
            if (disconnectedDelegate != nullptr) {
                disconnectedDelegate();
            }
        }

        // Twitch::Connection

        virtual void SetMessageReceivedDelegate(MessageReceivedDelegate messageReceivedDelegate) override {
            this->messageReceivedDelegate = messageReceivedDelegate;
        }

        virtual void SetDisconnectedDelegate(DisconnectedDelegate disconnectedDelegate) override {
            this->disconnectedDelegate = disconnectedDelegate;
        }

        virtual bool Connect() override {
            if (failConnectionAttempt) {
                return false;
            }
            if (isConnected) {
                connectionProblem = true;
                return false;
            }
            isConnected = true;
            return true;
        }

        virtual void Send(const std::string& message) override {
            if (!isConnected) {
                connectionProblem = true;
                return;
            }
            dataReceived += message;
            const auto lineEnd = dataReceived.find(CRLF);
            if (lineEnd == std::string::npos) {
                return;
            }
            const auto line = dataReceived.substr(0, lineEnd);
            std::lock_guard< std::mutex > lock(mutex);
            linesReceived.push_back(line);
            dataReceived = dataReceived.substr(lineEnd + CRLF.length());
            if (line.substr(0, 5) == "PASS ") {
                passwordOffered = line.substr(5);
                passwordOffered = SystemAbstractions::Trim(passwordOffered);
            } else if (line.substr(0, 5) == "NICK ") {
                nicknameOffered = line.substr(5);
                nicknameOffered = SystemAbstractions::Trim(nicknameOffered);
            }
            wakeCondition.notify_one();
        }

        virtual void Disconnect() override {
            isDisconnected = true;
        }
    };

    /**
     * This is a fake time-keeper which is used to test the Messaging class.
     */
    struct MockTimeKeeper
        : public Twitch::TimeKeeper
    {
        // Properties

        double currentTime = 0.0;

        // Methods

        // Twitch::TimeKeeper

        virtual double GetCurrentTime() override {
            return currentTime;
        }
    };

    /**
     * This contains all the information received for a single Join callback.
     */
    struct JoinInfo {
        /**
         * This is the channel where the user joined.
         */
        std::string channel;

        /**
         * This is the nickname of the user who joined.
         */
        std::string user;
    };

    /**
     * This represents the user of the unit under test, and receives all
     * notifications, events, and other callbacks from the unit under test.
     */
    struct User
        : public Twitch::Messaging::User
    {
        // Properties

        bool loggedIn = false;
        bool loggedOut = false;
        std::vector< JoinInfo > joins;
        std::condition_variable wakeCondition;
        std::mutex mutex;

        // Methods

        bool AwaitLogIn() {
            std::unique_lock< std::mutex > lock(mutex);
            return wakeCondition.wait_for(
                lock,
                std::chrono::milliseconds(100),
                [this]{ return loggedIn; }
            );
        }

        bool AwaitLogOut() {
            std::unique_lock< std::mutex > lock(mutex);
            return wakeCondition.wait_for(
                lock,
                std::chrono::milliseconds(100),
                [this]{ return loggedOut; }
            );
        }

        bool AwaitJoin() {
            std::unique_lock< std::mutex > lock(mutex);
            const auto numJoinsBefore = joins.size();
            return wakeCondition.wait_for(
                lock,
                std::chrono::milliseconds(100),
                [this, numJoinsBefore]{ return joins.size() != numJoinsBefore; }
            );
        }

        // Twitch::Messaging::User

        virtual void LogIn() override {
            std::lock_guard< std::mutex > lock(mutex);
            loggedIn = true;
            wakeCondition.notify_one();
        }

        virtual void LogOut() override {
            std::lock_guard< std::mutex > lock(mutex);
            loggedOut = true;
            wakeCondition.notify_one();
        }

        virtual void Join(
            const std::string& channel,
            const std::string& user
        ) override {
            std::lock_guard< std::mutex > lock(mutex);
            JoinInfo joinInfo;
            joinInfo.channel = channel;
            joinInfo.user = user;
            joins.push_back(joinInfo);
            wakeCondition.notify_one();
        }

    };

}

/**
 * This is the test fixture for these tests, providing common
 * setup and teardown for each test.
 */
struct MessagingTests
    : public ::testing::Test
{
    // Properties

    /**
     * This is the unit under test.
     */
    Twitch::Messaging tmi;

    /**
     * This represents the user of the unit under test, and receives all
     * notifications, events, and other callbacks from the unit under test.
     */
    std::shared_ptr< User > user = std::make_shared< User >();

    /**
     * This is used to simulate the Twitch server.
     */
    std::shared_ptr< MockServer > mockServer = std::make_shared< MockServer >();

    /**
     * This is used to simulate real time, when testing timeouts.
     */
    std::shared_ptr< MockTimeKeeper > mockTimeKeeper = std::make_shared< MockTimeKeeper >();

    /**
     * This flag keeps track of whether or not the unit under test has created
     * a connection.
     */
    bool connectionCreatedByUnitUnderTest = false;

    // Methods

    /**
     * This is a convenience method which performs all the necessary steps to
     * log into the mock Twitch server.
     */
    void LogIn() {
        const std::string nickname = "foobar1124";
        const std::string token = "alskdfjasdf87sdfsdffsd";
        tmi.LogIn(nickname, token);
        (void)mockServer->AwaitNickname();
        mockServer->ReturnToClient(
            ":tmi.twitch.tv 372 <user> :You are in a maze of twisty passages." + CRLF
            + ":tmi.twitch.tv 376 <user> :>" + CRLF
        );
        (void)user->AwaitLogIn();
        mockServer->ClearLinesReceived();
    }

    // ::testing::Test

    virtual void SetUp() {
        const auto connectionFactory = [this]() -> std::shared_ptr< Twitch::Connection > {
            if (connectionCreatedByUnitUnderTest) {
                mockServer = std::make_shared< MockServer >();
            }
            connectionCreatedByUnitUnderTest = true;
            return mockServer;
        };
        tmi.SetConnectionFactory(connectionFactory);
        tmi.SetTimeKeeper(mockTimeKeeper);
        tmi.SetUser(user);
    }

    virtual void TearDown() {
    }
};

TEST_F(MessagingTests, LogIntoChat) {
    const std::string nickname = "foobar1124";
    const std::string token = "alskdfjasdf87sdfsdffsd";
    tmi.LogIn(nickname, token);
    EXPECT_TRUE(mockServer->AwaitNickname());
    EXPECT_FALSE(user->AwaitLogIn());
    mockServer->ReturnToClient(
        ":tmi.twitch.tv 372 <user> :You are in a maze of twisty passages." + CRLF
        + ":tmi.twitch.tv 376 <user> :>" + CRLF
    );
    ASSERT_TRUE(user->AwaitLogIn());
    EXPECT_TRUE(mockServer->IsConnected());
    EXPECT_FALSE(mockServer->WasThereAConnectionProblem());
    EXPECT_EQ(nickname, mockServer->GetNicknameOffered());
    EXPECT_EQ(std::string("oauth:") + token, mockServer->GetPasswordOffered());
    EXPECT_EQ(
        (std::vector< std::string >{
            "PASS oauth:" + token,
            "NICK " + nickname,
        }),
        mockServer->GetLinesReceived()
    );
    EXPECT_FALSE(mockServer->IsDisconnected());
}

TEST_F(MessagingTests, LogOutOfChat) {
    LogIn();
    const std::string farewell = "See ya sucker!";
    tmi.LogOut(farewell);
    ASSERT_TRUE(user->AwaitLogOut());
    EXPECT_EQ(
        (std::vector< std::string >{
            "QUIT :" + farewell,
        }),
        mockServer->GetLinesReceived()
    );
    EXPECT_TRUE(mockServer->IsDisconnected());
}

TEST_F(MessagingTests, LogInWhenAlreadyLoggedIn) {
    // Log in normally, before the "test" begins.
    LogIn();

    // Try to log in again, while still logged in.
    user->loggedIn = false;
    const std::string nickname = "foobar1124";
    const std::string token = "alskdfjasdf87sdfsdffsd";
    tmi.LogIn(nickname, token);
    ASSERT_FALSE(user->AwaitLogIn());
}

TEST_F(MessagingTests, LogInFailureToConnect) {
    // First set up the mock server to simulate a connection failure.
    mockServer->FailConnectionAttempt();

    // Now try to log in.
    const std::string nickname = "foobar1124";
    const std::string token = "alskdfjasdf87sdfsdffsd";
    tmi.LogIn(nickname, token);
    ASSERT_TRUE(user->AwaitLogOut());
}

TEST_F(MessagingTests, ExtraMotdWhileAlreadyLoggedIn) {
    // Log in normally, before the "test" begins.
    LogIn();

    // Reset the "loggedIn" flag, to make sure we'll be able to see if it gets
    // set again when the extra MOTD is received.
    user->loggedIn = false;

    // Have the server send another MOTD, and make sure
    // we don't receive an extra logged-in callback.
    mockServer->ReturnToClient(
        ":tmi.twitch.tv 372 <user> :You are in a maze of twisty passages." + CRLF
        + ":tmi.twitch.tv 376 <user> :>" + CRLF
    );
    EXPECT_FALSE(user->AwaitLogIn());
}

TEST_F(MessagingTests, LogInFailureNoMotd) {
    const std::string nickname = "foobar1124";
    const std::string token = "alskdfjasdf87sdfsdffsd";
    tmi.LogIn(nickname, token);
    (void)mockServer->AwaitNickname();
    mockServer->ClearLinesReceived();
    ASSERT_FALSE(user->AwaitLogOut());
    mockTimeKeeper->currentTime = 5.0;
    EXPECT_TRUE(user->AwaitLogOut());
    EXPECT_FALSE(user->loggedIn);
    EXPECT_EQ(
        (std::vector< std::string >{
            "QUIT :Timeout waiting for MOTD"
        }),
        mockServer->GetLinesReceived()
    );
    EXPECT_TRUE(mockServer->IsDisconnected());
}

TEST_F(MessagingTests, LogInFailureUnexpectedDisconnect) {
    const std::string nickname = "foobar1124";
    const std::string token = "alskdfjasdf87sdfsdffsd";
    tmi.LogIn(nickname, token);
    EXPECT_TRUE(mockServer->AwaitNickname());
    EXPECT_FALSE(user->AwaitLogIn());
    mockServer->ClearLinesReceived();
    mockServer->DisconnectClient();
    EXPECT_TRUE(user->AwaitLogOut());
    EXPECT_FALSE(user->loggedIn);
    EXPECT_EQ(
        (std::vector< std::string >{
        }),
        mockServer->GetLinesReceived()
    );
    EXPECT_TRUE(mockServer->IsDisconnected());
}

TEST_F(MessagingTests, JoinChannel) {
    // Log in normally, before the "test" begins.
    LogIn();

    // Attempt to join a channel.  Wait for the mock server
    // to receive the JOIN command, and then have it issue back
    // the expected reponses.
    tmi.Join("foobar1125");
    EXPECT_TRUE(mockServer->AwaitLineReceived("JOIN #foobar1125"));
    mockServer->ReturnToClient(
        ":foobar1124!foobar1124@foobar1124.tmi.twitch.tv JOIN #foobar1125" + CRLF
    );
    ASSERT_TRUE(user->AwaitJoin());
    ASSERT_EQ(1, user->joins.size());
    EXPECT_EQ("foobar1125", user->joins[0].channel);
    EXPECT_EQ("foobar1124", user->joins[0].user);
}

TEST_F(MessagingTests, JoinChannelWhenNotConnected) {
    // TODO: Needs to be implemented
}

TEST_F(MessagingTests, LeaveChannel) {
    // TODO: Needs to be implemented
}

TEST_F(MessagingTests, ReceiveMessages) {
    // Log in normally, before the "test" begins.
    LogIn();

    // Have the pretend Twitch server simulate someone else chatting in the
    // room.

    // TODO: Needs to be implemented
}

TEST_F(MessagingTests, SendMessage) {
    // TODO: Needs to be implemented
}
