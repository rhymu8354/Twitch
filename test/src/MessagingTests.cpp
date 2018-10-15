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
        bool capLsReceived = false;
        bool capEndReceived = false;
        bool nickSetBeforeCapEnd = false;
        bool wasCapsRequested = false;
        std::string capsRequested;
        std::string capLsArg;
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

        bool AwaitCapLs() {
            std::unique_lock< std::mutex > lock(mutex);
            return wakeCondition.wait_for(
                lock,
                std::chrono::milliseconds(100),
                [this]{ return capLsReceived; }
            );
        }

        bool AwaitCapReq() {
            std::unique_lock< std::mutex > lock(mutex);
            return wakeCondition.wait_for(
                lock,
                std::chrono::milliseconds(100),
                [this]{ return wasCapsRequested; }
            );
        }

        bool AwaitCapEnd() {
            std::unique_lock< std::mutex > lock(mutex);
            return wakeCondition.wait_for(
                lock,
                std::chrono::milliseconds(100),
                [this]{ return capEndReceived; }
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
                if (!capEndReceived) {
                    nickSetBeforeCapEnd = true;
                }
                nicknameOffered = line.substr(5);
                nicknameOffered = SystemAbstractions::Trim(nicknameOffered);
            } else if (line.substr(0, 7) == "CAP LS ") {
                capLsReceived = true;
                capLsArg = line.substr(7);
            } else if (line.substr(0, 9) == "CAP REQ :") {
                wasCapsRequested = true;
                capsRequested = line.substr(9);
            } else if (line == "CAP END") {
                capEndReceived = true;
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
     * This represents the user of the unit under test, and receives all
     * notifications, events, and other callbacks from the unit under test.
     */
    struct User
        : public Twitch::Messaging::User
    {
        // Properties

        bool loggedIn = false;
        bool loggedOut = false;
        std::vector< Twitch::Messaging::MembershipInfo > joins;
        std::vector< Twitch::Messaging::MembershipInfo > parts;
        std::vector< Twitch::Messaging::MessageInfo > messages;
        std::vector< Twitch::Messaging::WhisperInfo > whispers;
        std::vector< std::string > notices;
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

        bool AwaitJoins(size_t numJoins) {
            std::unique_lock< std::mutex > lock(mutex);
            return wakeCondition.wait_for(
                lock,
                std::chrono::milliseconds(100),
                [this, numJoins]{ return joins.size() == numJoins; }
            );
        }

        bool AwaitLeaves(size_t numParts) {
            std::unique_lock< std::mutex > lock(mutex);
            return wakeCondition.wait_for(
                lock,
                std::chrono::milliseconds(100),
                [this, numParts]{ return parts.size() == numParts; }
            );
        }

        bool AwaitMessages(size_t numMessages) {
            std::unique_lock< std::mutex > lock(mutex);
            return wakeCondition.wait_for(
                lock,
                std::chrono::milliseconds(100),
                [this, numMessages]{ return messages.size() == numMessages; }
            );
        }

        bool AwaitWhispers(size_t numWhispers) {
            std::unique_lock< std::mutex > lock(mutex);
            return wakeCondition.wait_for(
                lock,
                std::chrono::milliseconds(100),
                [this, numWhispers]{ return whispers.size() == numWhispers; }
            );
        }

        bool AwaitNotices(size_t numNotices) {
            std::unique_lock< std::mutex > lock(mutex);
            return wakeCondition.wait_for(
                lock,
                std::chrono::milliseconds(100),
                [this, numNotices]{ return notices.size() == numNotices; }
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
            Twitch::Messaging::MembershipInfo&& membershipInfo
        ) override {
            std::lock_guard< std::mutex > lock(mutex);
            joins.push_back(std::move(membershipInfo));
            wakeCondition.notify_one();
        }

        virtual void Leave(
            Twitch::Messaging::MembershipInfo&& membershipInfo
        ) override {
            std::lock_guard< std::mutex > lock(mutex);
            parts.push_back(std::move(membershipInfo));
            wakeCondition.notify_one();
        }

        virtual void Message(
            Twitch::Messaging::MessageInfo&& messageInfo
        ) override {
            std::lock_guard< std::mutex > lock(mutex);
            messages.push_back(std::move(messageInfo));
            wakeCondition.notify_one();
        }

        virtual void Whisper(
            Twitch::Messaging::WhisperInfo&& whisperInfo
        ) override {
            std::lock_guard< std::mutex > lock(mutex);
            whispers.push_back(std::move(whisperInfo));
            wakeCondition.notify_one();
        }

        virtual void Notice(
            const std::string& message
        ) override {
            std::lock_guard< std::mutex > lock(mutex);
            notices.push_back(message);
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
        (void)mockServer->AwaitCapLs();
        mockServer->ReturnToClient(
            ":tmi.twitch.tv CAP * LS :twitch.tv/membership twitch.tv/tags twitch.tv/commands" + CRLF
        );
        (void)mockServer->AwaitCapReq();
        mockServer->ReturnToClient(
            ":tmi.twitch.tv CAP * ACK :twitch.tv/commands" + CRLF
        );
        (void)mockServer->AwaitNickname();
        mockServer->ReturnToClient(
            ":tmi.twitch.tv 372 <user> :You are in a maze of twisty passages." + CRLF
            + ":tmi.twitch.tv 376 <user> :>" + CRLF
        );
        (void)user->AwaitLogIn();
        mockServer->ClearLinesReceived();
    }

    /**
     * This is a convenience method which performs all the necessary steps to
     * join a channel.
     */
    void Join(const std::string& channel) {
        tmi.Join(channel);
        (void)mockServer->AwaitLineReceived("JOIN #" + channel);
        mockServer->ReturnToClient(
            ":foobar1124!foobar1124@foobar1124.tmi.twitch.tv JOIN #" + channel + CRLF
        );
        (void)user->AwaitJoins(1);
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

TEST_F(MessagingTests, DiagnosticsSubscription) {
    std::vector< std::string > capturedDiagnosticMessages;
    tmi.SubscribeToDiagnostics(
        [&capturedDiagnosticMessages](
            std::string senderName,
            size_t level,
            std::string message
        ){
            capturedDiagnosticMessages.push_back(
                SystemAbstractions::sprintf(
                    "%s[%zu]: %s",
                    senderName.c_str(),
                    level,
                    message.c_str()
                )
            );
        }
    );
    const std::string nickname = "foobar1124";
    const std::string token = "alskdfjasdf87sdfsdffsd";
    tmi.LogIn(nickname, token);
    (void)mockServer->AwaitCapLs();
    mockServer->ReturnToClient(
        ":tmi.twitch.tv CAP * LS :twitch.tv/membership twitch.tv/tags twitch.tv/commands" + CRLF
    );
    (void)mockServer->AwaitCapReq();
    mockServer->ReturnToClient(
        ":tmi.twitch.tv CAP * ACK :twitch.tv/commands" + CRLF
        + ":tmi.twitch.tv 372 <user> :You are in a maze of twisty passages." + CRLF
        + ":tmi.twitch.tv 376 <user> :>" + CRLF
    );
    (void)mockServer->AwaitNickname();
    (void)user->AwaitLogIn();
    EXPECT_EQ(
        (std::vector< std::string >{
            "TMI[0]: < CAP LS 302",
            "TMI[0]: > :tmi.twitch.tv CAP * LS :twitch.tv/membership twitch.tv/tags twitch.tv/commands",
            "TMI[0]: < CAP REQ :twitch.tv/commands",
            "TMI[0]: > :tmi.twitch.tv CAP * ACK :twitch.tv/commands",
            "TMI[0]: < CAP END",
            "TMI[0]: < PASS oauth:**********************",
            "TMI[0]: < NICK foobar1124",
            "TMI[0]: > :tmi.twitch.tv 372 <user> :You are in a maze of twisty passages.",
            "TMI[0]: > :tmi.twitch.tv 376 <user> :>",
        }),
        capturedDiagnosticMessages
    );
}

TEST_F(MessagingTests, DiagnosticsUnsubscription) {
    std::vector< std::string > capturedDiagnosticMessages;
    const auto unsubscribe = tmi.SubscribeToDiagnostics(
        [&capturedDiagnosticMessages](
            std::string senderName,
            size_t level,
            std::string message
        ){
            capturedDiagnosticMessages.push_back(
                SystemAbstractions::sprintf(
                    "%s[%zu]: %s",
                    senderName.c_str(),
                    level,
                    message.c_str()
                )
            );
        }
    );
    unsubscribe();
    const std::string nickname = "foobar1124";
    const std::string token = "alskdfjasdf87sdfsdffsd";
    tmi.LogIn(nickname, token);
    mockServer->ReturnToClient(
        ":tmi.twitch.tv CAP * LS :twitch.tv/membership twitch.tv/tags twitch.tv/commands" + CRLF
    );
    (void)mockServer->AwaitCapReq();
    mockServer->ReturnToClient(
        ":tmi.twitch.tv CAP * ACK :twitch.tv/commands" + CRLF
    );
    (void)mockServer->AwaitNickname();
    mockServer->ReturnToClient(
        ":tmi.twitch.tv 372 <user> :You are in a maze of twisty passages." + CRLF
        + ":tmi.twitch.tv 376 <user> :>" + CRLF
    );
    (void)user->AwaitLogIn();
    EXPECT_EQ(
        (std::vector< std::string >{
        }),
        capturedDiagnosticMessages
    );
}

TEST_F(MessagingTests, LogIntoChat) {
    const std::string nickname = "foobar1124";
    const std::string token = "alskdfjasdf87sdfsdffsd";
    tmi.LogIn(nickname, token);
    EXPECT_TRUE(mockServer->AwaitCapLs());
    EXPECT_EQ("302", mockServer->capLsArg);
    EXPECT_FALSE(mockServer->AwaitCapEnd());
    mockServer->ReturnToClient(
        ":tmi.twitch.tv CAP * LS :twitch.tv/membership twitch.tv/tags twitch.tv/commands" + CRLF
    );
    EXPECT_TRUE(mockServer->AwaitCapReq());
    EXPECT_EQ("twitch.tv/commands", mockServer->capsRequested);
    EXPECT_FALSE(mockServer->AwaitCapEnd());
    mockServer->ReturnToClient(
        ":tmi.twitch.tv CAP * ACK :twitch.tv/commands" + CRLF
    );
    EXPECT_TRUE(mockServer->AwaitCapEnd());
    EXPECT_TRUE(mockServer->AwaitNickname());
    EXPECT_FALSE(mockServer->nickSetBeforeCapEnd);
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
            "CAP LS 302",
            "CAP REQ :twitch.tv/commands",
            "CAP END",
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

TEST_F(MessagingTests, LogInFailureNoCaps) {
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
            "QUIT :Timeout waiting for capability list"
        }),
        mockServer->GetLinesReceived()
    );
    EXPECT_TRUE(mockServer->IsDisconnected());
}

TEST_F(MessagingTests, LogInFailureNoMotd) {
    const std::string nickname = "foobar1124";
    const std::string token = "alskdfjasdf87sdfsdffsd";
    tmi.LogIn(nickname, token);
    (void)mockServer->AwaitCapLs();
    mockServer->ReturnToClient(
        ":tmi.twitch.tv CAP * LS :twitch.tv/membership twitch.tv/tags twitch.tv/commands" + CRLF
    );
    (void)mockServer->AwaitCapReq();
    mockServer->ReturnToClient(
        ":tmi.twitch.tv CAP * ACK :twitch.tv/commands" + CRLF
    );
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

TEST_F(MessagingTests, LogInSuccessShouldNotPreceedADisconnectAfter5Seconds) {
    const std::string nickname = "foobar1124";
    const std::string token = "alskdfjasdf87sdfsdffsd";
    tmi.LogIn(nickname, token);
    (void)mockServer->AwaitCapLs();
    mockServer->ReturnToClient(
        ":tmi.twitch.tv CAP * LS :twitch.tv/membership twitch.tv/tags twitch.tv/commands" + CRLF
    );
    (void)mockServer->AwaitCapReq();
    mockServer->ReturnToClient(
        ":tmi.twitch.tv CAP * ACK :twitch.tv/commands" + CRLF
        + ":tmi.twitch.tv 372 <user> :You are in a maze of twisty passages." + CRLF
        + ":tmi.twitch.tv 376 <user> :>" + CRLF
    );
    (void)mockServer->AwaitNickname();
    mockServer->ReturnToClient(
        ":tmi.twitch.tv 372 <user> :You are in a maze of twisty passages." + CRLF
        + ":tmi.twitch.tv 376 <user> :>" + CRLF
    );
    (void)user->AwaitLogIn();
    mockServer->ClearLinesReceived();
    mockTimeKeeper->currentTime = 5.0;
    EXPECT_FALSE(user->AwaitLogOut());
    EXPECT_EQ(
        (std::vector< std::string >{
        }),
        mockServer->GetLinesReceived()
    );
    EXPECT_FALSE(mockServer->IsDisconnected());
}

TEST_F(MessagingTests, LogInFailureUnexpectedDisconnect) {
    const std::string nickname = "foobar1124";
    const std::string token = "alskdfjasdf87sdfsdffsd";
    tmi.LogIn(nickname, token);
    (void)mockServer->AwaitCapLs();
    mockServer->ReturnToClient(
        ":tmi.twitch.tv CAP * LS :twitch.tv/membership twitch.tv/tags twitch.tv/commands" + CRLF
    );
    (void)mockServer->AwaitCapReq();
    mockServer->ReturnToClient(
        ":tmi.twitch.tv CAP * ACK :twitch.tv/commands" + CRLF
    );
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

TEST_F(MessagingTests, LogInFailureBadCredentials) {
    const std::string nickname = "foobar1124";
    const std::string token = "alskdfjasdf87sdfsdffsd";
    tmi.LogIn(nickname, token);
    (void)mockServer->AwaitCapLs();
    mockServer->ReturnToClient(
        ":tmi.twitch.tv CAP * LS :twitch.tv/membership twitch.tv/tags twitch.tv/commands" + CRLF
    );
    (void)mockServer->AwaitCapReq();
    mockServer->ReturnToClient(
        ":tmi.twitch.tv CAP * ACK :twitch.tv/commands" + CRLF
    );
    EXPECT_TRUE(mockServer->AwaitNickname());
    EXPECT_FALSE(user->AwaitLogIn());
    mockServer->ClearLinesReceived();
    mockServer->ReturnToClient(
        ":tmi.twitch.tv NOTICE * :Login unsuccessful" + CRLF
    );
    EXPECT_FALSE(user->AwaitLogIn());
    EXPECT_TRUE(user->AwaitLogOut());
    EXPECT_FALSE(user->loggedIn);
    EXPECT_EQ(
        (std::vector< std::string >{
        }),
        mockServer->GetLinesReceived()
    );
    ASSERT_EQ(1, user->notices.size());
    EXPECT_EQ("Login unsuccessful", user->notices[0]);
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
    ASSERT_TRUE(user->AwaitJoins(1));
    EXPECT_EQ("foobar1125", user->joins[0].channel);
    EXPECT_EQ("foobar1124", user->joins[0].user);
}

TEST_F(MessagingTests, JoinChannelWhenNotConnected) {
    tmi.Join("foobar1125");
    EXPECT_FALSE(mockServer->AwaitLineReceived("JOIN #foobar1125"));
}

TEST_F(MessagingTests, LeaveChannel) {
    // Log in and join a channel.
    LogIn();
    Join("foobar1125");

    // Attempt to leave a channel.  Wait for the mock server
    // to receive the PART command, and then have it issue back
    // the expected reponses.
    tmi.Leave("foobar1125");
    EXPECT_TRUE(mockServer->AwaitLineReceived("PART #foobar1125"));
    mockServer->ReturnToClient(
        ":foobar1124!foobar1124@foobar1124.tmi.twitch.tv PART #foobar1125" + CRLF
    );
    ASSERT_TRUE(user->AwaitLeaves(1));
    EXPECT_EQ("foobar1125", user->parts[0].channel);
    EXPECT_EQ("foobar1124", user->parts[0].user);
}

TEST_F(MessagingTests, LeaveChannelWhenNotConnected) {
    tmi.Leave("foobar1125");
    EXPECT_FALSE(mockServer->AwaitLineReceived("PART #foobar1125"));
}

TEST_F(MessagingTests, ReceiveMessages) {
    // Log in and join a channel.
    LogIn();
    Join("foobar1125");

    // Have the pretend Twitch server simulate someone else chatting in the
    // room.
    mockServer->ReturnToClient(
        ":foobar1126!foobar1126@foobar1126.tmi.twitch.tv PRIVMSG #foobar1125 :Hello, World!" + CRLF
    );

    // Wait for the message to be received.
    ASSERT_TRUE(user->AwaitMessages(1));
    ASSERT_EQ(1, user->messages.size());
    EXPECT_EQ("foobar1125", user->messages[0].channel);
    EXPECT_EQ("foobar1126", user->messages[0].user);
    EXPECT_EQ("Hello, World!", user->messages[0].message);
}

TEST_F(MessagingTests, SendMessage) {
    // Log in and join a channel.
    LogIn();
    Join("foobar1125");

    // Send a message to the channel we just joined.
    tmi.SendMessage("foobar1125", "Hello, World!");
    EXPECT_TRUE(mockServer->AwaitLineReceived("PRIVMSG #foobar1125 :Hello, World!"));
}

TEST_F(MessagingTests, SendMessageWhenNotConnected) {
    tmi.SendMessage("foobar1125", "Hello, World!");
    EXPECT_FALSE(mockServer->AwaitLineReceived("PRIVMSG #foobar1125 :Hello, World!"));
}

TEST_F(MessagingTests, Ping) {
    // Log in and then clear the received lines buffer.
    LogIn();
    mockServer->ClearLinesReceived();

    // Have the pretend Twitch server simulate a PING message.
    mockServer->ReturnToClient(
        "PING :Hello!" + CRLF
        + "PING :Are you there?" + CRLF
    );

    // Wait for the user agent to respond with a corresponding PONG message.
    ASSERT_TRUE(mockServer->AwaitLineReceived("PONG :Are you there?")) << mockServer->GetLinesReceived().size();
    EXPECT_EQ(
        (std::vector< std::string >{
            "PONG :Hello!",
            "PONG :Are you there?"
        }),
        mockServer->GetLinesReceived()
    );
}

TEST_F(MessagingTests, CommandCapabilityNotRequestedWhenNotSupported) {
    const std::string nickname = "foobar1124";
    const std::string token = "alskdfjasdf87sdfsdffsd";
    tmi.LogIn(nickname, token);
    (void)mockServer->AwaitCapLs();
    mockServer->ReturnToClient(
        ":tmi.twitch.tv CAP * LS :twitch.tv/membership twitch.tv/tags" + CRLF
    );
    EXPECT_TRUE(mockServer->AwaitCapEnd());
    EXPECT_FALSE(mockServer->wasCapsRequested);
    EXPECT_TRUE(mockServer->AwaitNickname());
    EXPECT_FALSE(mockServer->nickSetBeforeCapEnd);
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
            "CAP LS 302",
            "CAP END",
            "PASS oauth:" + token,
            "NICK " + nickname,
        }),
        mockServer->GetLinesReceived()
    );
    EXPECT_FALSE(mockServer->IsDisconnected());
}

TEST_F(MessagingTests, ReceiveWhisper) {
    // Just log in.
    // There's no need to join any channel in order to send/receive whispers.
    LogIn();

    // Have the pretend Twitch server simulate someone else sending us a
    // whisper.
    mockServer->ReturnToClient(
        ":foobar1126!foobar1126@foobar1126.tmi.twitch.tv WHISPER foobar1124 :Hello, World!" + CRLF
    );

    // Wait for the message to be received.
    ASSERT_TRUE(user->AwaitWhispers(1));
    ASSERT_EQ(1, user->whispers.size());
    EXPECT_EQ("foobar1126", user->whispers[0].user);
    EXPECT_EQ("Hello, World!", user->whispers[0].message);
}

TEST_F(MessagingTests, SendWhisper) {
    // Just log in.
    // There's no need to join any channel in order to send/receive whispers.
    LogIn();

    // Send a message to the channel we just joined.
    tmi.SendWhisper("foobar1126", "Hello, World!");
    EXPECT_TRUE(mockServer->AwaitLineReceived("PRIVMSG #jtv :.w foobar1126 Hello, World!"));
}

TEST_F(MessagingTests, ReceiveGenericNotice) {
    // Just log in.
    // There's no need to join any channel in order to receive notices.
    LogIn();

    // Have the pretend Twitch server simulate some kind of generic notice.
    mockServer->ReturnToClient(
        ":foobar1126!foobar1126@foobar1126.tmi.twitch.tv NOTICE * :Grey is the new black!" + CRLF
    );

    // Wait for the message to be received.
    ASSERT_TRUE(user->AwaitNotices(1));
    ASSERT_EQ(1, user->notices.size());
    EXPECT_EQ("Grey is the new black!", user->notices[0]);
}
