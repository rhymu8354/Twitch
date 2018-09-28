/**
 * @file MessagingTests.cpp
 *
 * This module contains the unit tests of the Twitch::Messaging class.
 *
 * © 2018 by Richard Walters
 */

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
            linesReceived.push_back(line);
            dataReceived = dataReceived.substr(lineEnd + CRLF.length());
            if (line.substr(0, 5) == "PASS ") {
                passwordOffered = line.substr(5);
                passwordOffered = SystemAbstractions::Trim(passwordOffered);
            } else if (line.substr(0, 5) == "NICK ") {
                std::lock_guard< std::mutex > lock(mutex);
                nicknameOffered = line.substr(5);
                nicknameOffered = SystemAbstractions::Trim(nicknameOffered);
                wakeCondition.notify_one();
            }
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
        bool loggedIn = false;
        std::condition_variable wakeCondition;
        std::mutex mutex;
        tmi.SetLoggedInDelegate(
            [
                &loggedIn,
                &wakeCondition,
                &mutex
            ]{
                std::lock_guard< std::mutex > lock(mutex);
                loggedIn = true;
                wakeCondition.notify_one();
            }
        );
        const std::string nickname = "ninja";
        const std::string token = "alskdfjasdf87sdfsdffsd";
        tmi.LogIn(nickname, token);
        (void)mockServer->AwaitNickname();
        mockServer->ReturnToClient(
            ":tmi.twitch.tv 372 <user> :You are in a maze of twisty passages." + CRLF
            + ":tmi.twitch.tv 376 <user> :>" + CRLF
        );
        {
            std::unique_lock< std::mutex > lock(mutex);
            (void)wakeCondition.wait_for(
                lock,
                std::chrono::milliseconds(100),
                [&loggedIn]{ return loggedIn; }
            );
        }
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
    }

    virtual void TearDown() {
    }
};

TEST_F(MessagingTests, LogIntoChat) {
    bool loggedIn = false;
    std::condition_variable wakeCondition;
    std::mutex mutex;
    tmi.SetLoggedInDelegate(
        [
            &loggedIn,
            &wakeCondition,
            &mutex
        ]{
            std::lock_guard< std::mutex > lock(mutex);
            loggedIn = true;
            wakeCondition.notify_one();
        }
    );
    const std::string nickname = "ninja";
    const std::string token = "alskdfjasdf87sdfsdffsd";
    tmi.LogIn(nickname, token);
    EXPECT_TRUE(mockServer->AwaitNickname());
    {
        std::unique_lock< std::mutex > lock(mutex);
        EXPECT_FALSE(
            wakeCondition.wait_for(
                lock,
                std::chrono::milliseconds(100),
                [&loggedIn]{ return loggedIn; }
            )
        );
    }
    mockServer->ReturnToClient(
        ":tmi.twitch.tv 372 <user> :You are in a maze of twisty passages." + CRLF
        + ":tmi.twitch.tv 376 <user> :>" + CRLF
    );
    {
        std::unique_lock< std::mutex > lock(mutex);
        ASSERT_TRUE(
            wakeCondition.wait_for(
                lock,
                std::chrono::milliseconds(100),
                [&loggedIn]{ return loggedIn; }
            )
        );
    }
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
    bool loggedOut = false;
    std::condition_variable wakeCondition;
    std::mutex mutex;
    tmi.SetLoggedOutDelegate(
        [
            &loggedOut,
            &wakeCondition,
            &mutex
        ]{
            std::lock_guard< std::mutex > lock(mutex);
            loggedOut = true;
            wakeCondition.notify_one();
        }
    );
    LogIn();
    const std::string farewell = "See ya sucker!";
    tmi.LogOut(farewell);
    {
        std::unique_lock< std::mutex > lock(mutex);
        ASSERT_TRUE(
            wakeCondition.wait_for(
                lock,
                std::chrono::milliseconds(100),
                [&loggedOut]{ return loggedOut; }
            )
        );
    }
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
    bool loggedIn = false;
    std::condition_variable wakeCondition;
    std::mutex mutex;
    tmi.SetLoggedInDelegate(
        [
            &loggedIn,
            &wakeCondition,
            &mutex
        ]{
            std::lock_guard< std::mutex > lock(mutex);
            loggedIn = true;
            wakeCondition.notify_one();
        }
    );
    const std::string nickname = "ninja";
    const std::string token = "alskdfjasdf87sdfsdffsd";
    tmi.LogIn(nickname, token);
    {
        std::unique_lock< std::mutex > lock(mutex);
        ASSERT_FALSE(
            wakeCondition.wait_for(
                lock,
                std::chrono::milliseconds(100),
                [&loggedIn]{ return loggedIn; }
            )
        );
    }
}

TEST_F(MessagingTests, LogInFailureToConnect) {
    // First set up the mock server to simulate a connection failure.
    mockServer->FailConnectionAttempt();

    // Now try to log in.
    bool loggedOut = false;
    std::condition_variable wakeCondition;
    std::mutex mutex;
    tmi.SetLoggedOutDelegate(
        [
            &loggedOut,
            &wakeCondition,
            &mutex
        ]{
            std::lock_guard< std::mutex > lock(mutex);
            loggedOut = true;
            wakeCondition.notify_one();
        }
    );
    const std::string nickname = "ninja";
    const std::string token = "alskdfjasdf87sdfsdffsd";
    tmi.LogIn(nickname, token);
    {
        std::unique_lock< std::mutex > lock(mutex);
        ASSERT_TRUE(
            wakeCondition.wait_for(
                lock,
                std::chrono::milliseconds(100),
                [&loggedOut]{ return loggedOut; }
            )
        );
    }
}

TEST_F(MessagingTests, ExtraMotdWhileAlreadyLoggedIn) {
    // Log in normally, before the "test" begins.
    LogIn();

    // Have the server send another MOTD, and make sure
    // we don't receive an extra logged-in callback.
    bool loggedIn = false;
    std::condition_variable wakeCondition;
    std::mutex mutex;
    tmi.SetLoggedInDelegate(
        [
            &loggedIn,
            &wakeCondition,
            &mutex
        ]{
            std::lock_guard< std::mutex > lock(mutex);
            loggedIn = true;
            wakeCondition.notify_one();
        }
    );
    mockServer->ReturnToClient(
        ":tmi.twitch.tv 372 <user> :You are in a maze of twisty passages." + CRLF
        + ":tmi.twitch.tv 376 <user> :>" + CRLF
    );
    {
        std::unique_lock< std::mutex > lock(mutex);
        EXPECT_FALSE(
            wakeCondition.wait_for(
                lock,
                std::chrono::milliseconds(100),
                [&loggedIn]{ return loggedIn; }
            )
        );
    }
}

TEST_F(MessagingTests, LogInFailureNoMotd) {
    bool loggedIn = false;
    bool loggedOut = false;
    std::condition_variable wakeCondition;
    std::mutex mutex;
    tmi.SetLoggedInDelegate(
        [
            &loggedIn,
            &wakeCondition,
            &mutex
        ]{
            std::lock_guard< std::mutex > lock(mutex);
            loggedIn = true;
            wakeCondition.notify_one();
        }
    );
    tmi.SetLoggedOutDelegate(
        [
            &loggedOut,
            &wakeCondition,
            &mutex
        ]{
            std::lock_guard< std::mutex > lock(mutex);
            loggedOut = true;
            wakeCondition.notify_one();
        }
    );
    const std::string nickname = "ninja";
    const std::string token = "alskdfjasdf87sdfsdffsd";
    tmi.LogIn(nickname, token);
    (void)mockServer->AwaitNickname();
    mockServer->ClearLinesReceived();
    {
        std::unique_lock< std::mutex > lock(mutex);
        ASSERT_FALSE(
            wakeCondition.wait_for(
                lock,
                std::chrono::milliseconds(100),
                [&loggedOut]{ return loggedOut; }
            )
        );
    }
    mockTimeKeeper->currentTime = 5.0;
    {
        std::unique_lock< std::mutex > lock(mutex);
        EXPECT_TRUE(
            wakeCondition.wait_for(
                lock,
                std::chrono::milliseconds(100),
                [&loggedOut]{ return loggedOut; }
            )
        );
    }
    EXPECT_FALSE(loggedIn);
    EXPECT_EQ(
        (std::vector< std::string >{
            "QUIT :Timeout waiting for MOTD"
        }),
        mockServer->GetLinesReceived()
    );
    EXPECT_TRUE(mockServer->IsDisconnected());
}

TEST_F(MessagingTests, LogInFailureUnexpectedDisconnect) {
    bool loggedIn = false;
    bool loggedOut = false;
    std::condition_variable wakeCondition;
    std::mutex mutex;
    tmi.SetLoggedInDelegate(
        [
            &loggedIn,
            &wakeCondition,
            &mutex
        ]{
            std::lock_guard< std::mutex > lock(mutex);
            loggedIn = true;
            wakeCondition.notify_one();
        }
    );
    tmi.SetLoggedOutDelegate(
        [
            &loggedOut,
            &wakeCondition,
            &mutex
        ]{
            std::lock_guard< std::mutex > lock(mutex);
            loggedOut = true;
            wakeCondition.notify_one();
        }
    );
    const std::string nickname = "ninja";
    const std::string token = "alskdfjasdf87sdfsdffsd";
    tmi.LogIn(nickname, token);
    EXPECT_TRUE(mockServer->AwaitNickname());
    {
        std::unique_lock< std::mutex > lock(mutex);
        EXPECT_FALSE(
            wakeCondition.wait_for(
                lock,
                std::chrono::milliseconds(100),
                [&loggedIn]{ return loggedIn; }
            )
        );
    }
    mockServer->ClearLinesReceived();
    mockServer->DisconnectClient();
    {
        std::unique_lock< std::mutex > lock(mutex);
        EXPECT_TRUE(
            wakeCondition.wait_for(
                lock,
                std::chrono::milliseconds(100),
                [&loggedOut]{ return loggedOut; }
            )
        );
    }
    EXPECT_FALSE(loggedIn);
    EXPECT_EQ(
        (std::vector< std::string >{
        }),
        mockServer->GetLinesReceived()
    );
    EXPECT_TRUE(mockServer->IsDisconnected());
}

TEST_F(MessagingTests, ReceiveMessages) {
}

TEST_F(MessagingTests, SendMessage) {
}
