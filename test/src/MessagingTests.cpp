/**
 * @file MessagingTests.cpp
 *
 * This module contains the unit tests of the Twitch::Messaging class.
 *
 * © 2018 by Richard Walters
 */

#include <algorithm>
#include <condition_variable>
#include <future>
#include <gtest/gtest.h>
#include <memory>
#include <mutex>
#include <regex>
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
     * This regular expression should only match the nickname of an anonymous
     * Twitch user.
     */
    static const std::regex ANONYMOUS_NICKNAME_PATTERN("justinfan([0-9]+)");

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
        bool wasPasswordOffered = false;
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

        bool WasPasswordOffered() {
            return wasPasswordOffered;
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
                wasPasswordOffered = true;
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
        bool doom = false;
        std::vector< Twitch::Messaging::MembershipInfo > joins;
        std::vector< Twitch::Messaging::MembershipInfo > parts;
        std::vector< Twitch::Messaging::MessageInfo > messages;
        std::vector< Twitch::Messaging::MessageInfo > privateMessages;
        std::vector< Twitch::Messaging::WhisperInfo > whispers;
        std::vector< Twitch::Messaging::NoticeInfo > notices;
        std::vector< Twitch::Messaging::HostInfo > hosts;
        std::vector< Twitch::Messaging::RoomModeChangeInfo > roomModeChanges;
        std::vector< Twitch::Messaging::ClearInfo > clears;
        std::vector< Twitch::Messaging::ModInfo > mods;
        std::vector< Twitch::Messaging::UserStateInfo > userStates;
        std::vector< Twitch::Messaging::SubInfo > subs;
        std::vector< Twitch::Messaging::RaidInfo > raids;
        std::vector< Twitch::Messaging::RitualInfo > rituals;
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

        bool AwaitPrivateMessages(size_t numPrivateMessages) {
            std::unique_lock< std::mutex > lock(mutex);
            return wakeCondition.wait_for(
                lock,
                std::chrono::milliseconds(100),
                [this, numPrivateMessages]{ return privateMessages.size() == numPrivateMessages; }
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

        bool AwaitHosts(size_t numHosts) {
            std::unique_lock< std::mutex > lock(mutex);
            return wakeCondition.wait_for(
                lock,
                std::chrono::milliseconds(100),
                [this, numHosts]{ return hosts.size() == numHosts; }
            );
        }

        bool AwaitRoomModeChanges(size_t numRoomModeChanges) {
            std::unique_lock< std::mutex > lock(mutex);
            return wakeCondition.wait_for(
                lock,
                std::chrono::milliseconds(100),
                [this, numRoomModeChanges]{ return roomModeChanges.size() == numRoomModeChanges; }
            );
        }

        bool AwaitClears(size_t numClears) {
            std::unique_lock< std::mutex > lock(mutex);
            return wakeCondition.wait_for(
                lock,
                std::chrono::milliseconds(100),
                [this, numClears]{ return clears.size() == numClears; }
            );
        }

        bool AwaitMods(size_t numMods) {
            std::unique_lock< std::mutex > lock(mutex);
            return wakeCondition.wait_for(
                lock,
                std::chrono::milliseconds(100),
                [this, numMods]{ return mods.size() == numMods; }
            );
        }

        bool AwaitUserState(size_t numUserStates) {
            std::unique_lock< std::mutex > lock(mutex);
            return wakeCondition.wait_for(
                lock,
                std::chrono::milliseconds(100),
                [this, numUserStates]{ return userStates.size() == numUserStates; }
            );
        }

        bool AwaitDoom() {
            std::unique_lock< std::mutex > lock(mutex);
            return wakeCondition.wait_for(
                lock,
                std::chrono::milliseconds(100),
                [this]{ return doom; }
            );
        }

        bool AwaitSubs(size_t numSubs) {
            std::unique_lock< std::mutex > lock(mutex);
            return wakeCondition.wait_for(
                lock,
                std::chrono::milliseconds(100),
                [this, numSubs]{ return subs.size() == numSubs; }
            );
        }

        bool AwaitRaids(size_t numRaids) {
            std::unique_lock< std::mutex > lock(mutex);
            return wakeCondition.wait_for(
                lock,
                std::chrono::milliseconds(100),
                [this, numRaids]{ return raids.size() == numRaids; }
            );
        }

        bool AwaitRituals(size_t numRituals) {
            std::unique_lock< std::mutex > lock(mutex);
            return wakeCondition.wait_for(
                lock,
                std::chrono::milliseconds(100),
                [this, numRituals]{ return rituals.size() == numRituals; }
            );
        }

        // Twitch::Messaging::User

        virtual void Doom() override {
            std::lock_guard< std::mutex > lock(mutex);
            doom = true;
            wakeCondition.notify_one();
        }

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

        virtual void PrivateMessage(
            Twitch::Messaging::MessageInfo&& messageInfo
        ) override {
            std::lock_guard< std::mutex > lock(mutex);
            privateMessages.push_back(std::move(messageInfo));
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
            Twitch::Messaging::NoticeInfo&& noticeInfo
        ) override {
            std::lock_guard< std::mutex > lock(mutex);
            notices.push_back(std::move(noticeInfo));
            wakeCondition.notify_one();
        }

        virtual void Host(
            Twitch::Messaging::HostInfo&& hostInfo
        ) override {
            std::lock_guard< std::mutex > lock(mutex);
            hosts.push_back(std::move(hostInfo));
            wakeCondition.notify_one();
        }

        virtual void RoomModeChange(
            Twitch::Messaging::RoomModeChangeInfo&& roomModeChangeInfo
        ) override {
            std::lock_guard< std::mutex > lock(mutex);
            roomModeChanges.push_back(std::move(roomModeChangeInfo));
            wakeCondition.notify_one();
        }

        virtual void Clear(
            Twitch::Messaging::ClearInfo&& clearInfo
        ) override {
            std::lock_guard< std::mutex > lock(mutex);
            clears.push_back(std::move(clearInfo));
            wakeCondition.notify_one();
        }

        virtual void Mod(
            Twitch::Messaging::ModInfo&& modInfo
        ) override {
            std::lock_guard< std::mutex > lock(mutex);
            mods.push_back(std::move(modInfo));
            wakeCondition.notify_one();
        }

        virtual void UserState(
            Twitch::Messaging::UserStateInfo&& userStateInfo
        ) override {
            std::lock_guard< std::mutex > lock(mutex);
            userStates.push_back(std::move(userStateInfo));
            wakeCondition.notify_one();
        }

        virtual void Sub(
            Twitch::Messaging::SubInfo&& subInfo
        ) override {
            std::lock_guard< std::mutex > lock(mutex);
            subs.push_back(std::move(subInfo));
            wakeCondition.notify_one();
        }

        virtual void Raid(
            Twitch::Messaging::RaidInfo&& raidInfo
        ) override {
            std::lock_guard< std::mutex > lock(mutex);
            raids.push_back(std::move(raidInfo));
            wakeCondition.notify_one();
        }

        virtual void Ritual(
            Twitch::Messaging::RitualInfo&& ritualInfo
        ) override {
            std::lock_guard< std::mutex > lock(mutex);
            rituals.push_back(std::move(ritualInfo));
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

    std::shared_ptr< std::promise< void > > newConnectionMade;

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
     *
     * @param[in] includeTags
     *     If true, also request the twitch.tv/tags capability from the server
     */
    void LogIn(bool includeTags = false) {
        const std::string nickname = "foobar1124";
        const std::string token = "alskdfjasdf87sdfsdffsd";
        tmi.LogIn(nickname, token);
        (void)mockServer->AwaitCapLs();
        mockServer->ReturnToClient(
            ":tmi.twitch.tv CAP * LS :twitch.tv/membership twitch.tv/tags twitch.tv/commands" + CRLF
        );
        (void)mockServer->AwaitCapReq();
        if (includeTags) {
            mockServer->ReturnToClient(
                ":tmi.twitch.tv CAP * ACK :twitch.tv/commands twitch.tv/tags" + CRLF
            );
        } else {
            mockServer->ReturnToClient(
                ":tmi.twitch.tv CAP * ACK :twitch.tv/commands" + CRLF
            );
        }
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
    void Join(
        const std::string& channel,
        const std::string& nickname = "foobar1124"
    ) {
        tmi.Join(channel);
        (void)mockServer->AwaitLineReceived("JOIN #" + channel);
        mockServer->ReturnToClient(
            SystemAbstractions::sprintf(
                ":%s!%s@%s.tmi.twitch.tv JOIN #%s%s",
                nickname.c_str(), nickname.c_str(), nickname.c_str(),
                channel.c_str(), CRLF.c_str()
            )
        );
        if (!std::regex_match(nickname, ANONYMOUS_NICKNAME_PATTERN)) {
            (void)user->AwaitJoins(1);
        }
    }

    /**
     * This is a convenience method which performs all the necessary steps to
     * leave a channel.
     */
    void Leave(
        const std::string& channel,
        const std::string& nickname = "foobar1124"
    ) {
        tmi.Leave(channel);
        (void)mockServer->AwaitLineReceived("PART #" + channel);
        mockServer->ReturnToClient(
            SystemAbstractions::sprintf(
                ":%s!%s@%s.tmi.twitch.tv PART #%s%s",
                nickname.c_str(), nickname.c_str(), nickname.c_str(),
                channel.c_str(), CRLF.c_str()
            )
        );
        if (!std::regex_match(nickname, ANONYMOUS_NICKNAME_PATTERN)) {
            (void)user->AwaitLeaves(1);
        }
    }

    // ::testing::Test

    virtual void SetUp() override {
        const auto connectionFactory = [this]() -> std::shared_ptr< Twitch::Connection > {
            if (connectionCreatedByUnitUnderTest) {
                mockServer = std::make_shared< MockServer >();
                if (newConnectionMade) {
                    newConnectionMade->set_value();
                }
            }
            connectionCreatedByUnitUnderTest = true;
            return mockServer;
        };
        tmi.SetConnectionFactory(connectionFactory);
        tmi.SetTimeKeeper(mockTimeKeeper);
        tmi.SetUser(user);
    }

    virtual void TearDown() override {
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
        ":tmi.twitch.tv CAP * ACK :twitch.tv/commands twitch.tv/membership twitch.tv/tags" + CRLF
        + ":tmi.twitch.tv 372 <user> :You are in a maze of twisty passages." + CRLF
        + ":tmi.twitch.tv 376 <user> :>" + CRLF
    );
    (void)mockServer->AwaitNickname();
    (void)user->AwaitLogIn();
    EXPECT_EQ(
        (std::vector< std::string >{
            "TMI[0]: < CAP LS 302",
            "TMI[0]: > :tmi.twitch.tv CAP * LS :twitch.tv/membership twitch.tv/tags twitch.tv/commands",
            "TMI[0]: < CAP REQ :twitch.tv/commands twitch.tv/membership twitch.tv/tags",
            "TMI[0]: > :tmi.twitch.tv CAP * ACK :twitch.tv/commands twitch.tv/membership twitch.tv/tags",
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

TEST_F(MessagingTests, NewConnectionForLogInAfterDisconnect) {
    const std::string nickname = "foobar1124";
    const std::string token = "alskdfjasdf87sdfsdffsd";
    tmi.LogIn(nickname, token);
    auto firstMockServer = mockServer;
    ASSERT_TRUE(mockServer->AwaitCapLs());
    mockServer->DisconnectClient();
    ASSERT_TRUE(user->AwaitLogOut());
    newConnectionMade = std::make_shared< std::promise< void > >();
    tmi.LogIn(nickname, token);
    ASSERT_TRUE(
        newConnectionMade->get_future().wait_for(std::chrono::milliseconds(100))
        == std::future_status::ready
    );
    EXPECT_FALSE(mockServer == firstMockServer);
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
    EXPECT_EQ(
        "twitch.tv/commands twitch.tv/membership twitch.tv/tags",
        mockServer->capsRequested
    );
    EXPECT_FALSE(mockServer->AwaitCapEnd());
    mockServer->ReturnToClient(
        ":tmi.twitch.tv CAP * ACK :twitch.tv/commands twitch.tv/membership twitch.tv/tags" + CRLF
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
            "CAP REQ :twitch.tv/commands twitch.tv/membership twitch.tv/tags",
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
        ":tmi.twitch.tv NOTICE * :Login authentication failed" + CRLF
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
    EXPECT_EQ("Login authentication failed", user->notices[0].message);
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

TEST_F(MessagingTests, ReceiveMessagesNoTagsCapability) {
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
    EXPECT_EQ("Hello, World!", user->messages[0].messageContent);
}

TEST_F(MessagingTests, ReceiveMessagesWithTagsCapabilityNoBits) {
    // Log in (with tags capability) and join a channel.
    LogIn(true);
    Join("foobar1125");

    // Have the pretend Twitch server simulate someone else chatting in the
    // room.
    mockServer->ReturnToClient(
        // tags
        "@badges=moderator/1,subscriber/12,partner/1;"
        "color=#5B99FF;"
        "display-name=FooBarMaster;"
        "emotes=30259:6-12,54-60/64138:29-37;"
        "flags=;"
        "id=1122aa44-55ff-ee88-11cc-1122dd44bb66;"
        "mod=1;"
        "room-id=12345;"
        "subscriber=1;"
        "tmi-sent-ts=1539652354185;"
        "turbo=0;"
        "user-id=54321;"
        "user-type=mod "

        // prefix
        ":foobar1126!foobar1126@foobar1126.tmi.twitch.tv "

        // command
        "PRIVMSG "

        // arguments
        "#foobar1125 :Hello HeyGuys This is a test SeemsGood Also did I say HeyGuys hello?" + CRLF
    );

    // Wait for the message to be received.
    ASSERT_TRUE(user->AwaitMessages(1));
    ASSERT_EQ(1, user->messages.size());
    EXPECT_FALSE(user->messages[0].isAction);
    EXPECT_EQ("foobar1125", user->messages[0].channel);
    EXPECT_EQ("foobar1126", user->messages[0].user);
    EXPECT_EQ("1122aa44-55ff-ee88-11cc-1122dd44bb66", user->messages[0].messageId);
    EXPECT_EQ("Hello HeyGuys This is a test SeemsGood Also did I say HeyGuys hello?", user->messages[0].messageContent);
    EXPECT_EQ(54321, user->messages[0].tags.userId);
    EXPECT_EQ(12345, user->messages[0].tags.channelId);
    EXPECT_EQ(1539652354, user->messages[0].tags.timestamp);
    EXPECT_EQ(185, user->messages[0].tags.timeMilliseconds);
    EXPECT_EQ("FooBarMaster", user->messages[0].tags.displayName);
    EXPECT_EQ(
        (std::set< std::string >{
            "moderator/1",
            "subscriber/12",
            "partner/1",
        }),
        user->messages[0].tags.badges
    );
    EXPECT_EQ(
        (std::map< int, std::vector< std::pair< int, int > > >{
            {30259, {{6, 12}, {54, 60}}},
            {64138, {{29, 37}}},
        }),
        user->messages[0].tags.emotes
    );
    EXPECT_EQ(0x5B99FF, user->messages[0].tags.color);
    EXPECT_EQ(0, user->messages[0].bits);
}

TEST_F(MessagingTests, ReceiveMessagesWithTagsCapabilityWithBits) {
    // Log in (with tags capability) and join a channel.
    LogIn(true);
    Join("foobar1125");

    // Have the pretend Twitch server simulate someone else chatting in the
    // room.
    mockServer->ReturnToClient(
        // tags
        "@badges=moderator/1,subscriber/12,partner/1;"
        "bits=100;"
        "color=#5B99FF;"
        "display-name=FooBarMaster;"
        "emotes=;"
        "flags=;"
        "id=1122aa44-55ff-ee88-11cc-1122dd44bb66;"
        "mod=1;"
        "room-id=12345;"
        "subscriber=1;"
        "tmi-sent-ts=1539652354185;"
        "turbo=0;"
        "user-id=54321;"
        "user-type=mod "

        // prefix
        ":foobar1126!foobar1126@foobar1126.tmi.twitch.tv "

        // command
        "PRIVMSG "

        // arguments
        "#foobar1125 :cheer100 Grats!" + CRLF
    );

    // Wait for the message to be received.
    ASSERT_TRUE(user->AwaitMessages(1));
    ASSERT_EQ(1, user->messages.size());
    EXPECT_FALSE(user->messages[0].isAction);
    EXPECT_EQ("foobar1125", user->messages[0].channel);
    EXPECT_EQ("foobar1126", user->messages[0].user);
    EXPECT_EQ("1122aa44-55ff-ee88-11cc-1122dd44bb66", user->messages[0].messageId);
    EXPECT_EQ("cheer100 Grats!", user->messages[0].messageContent);
    EXPECT_EQ(54321, user->messages[0].tags.userId);
    EXPECT_EQ(12345, user->messages[0].tags.channelId);
    EXPECT_EQ(1539652354, user->messages[0].tags.timestamp);
    EXPECT_EQ(185, user->messages[0].tags.timeMilliseconds);
    EXPECT_EQ("FooBarMaster", user->messages[0].tags.displayName);
    EXPECT_EQ(
        (std::set< std::string >{
            "moderator/1",
            "subscriber/12",
            "partner/1",
        }),
        user->messages[0].tags.badges
    );
    EXPECT_EQ(
        (std::map< int, std::vector< std::pair< int, int > > >{
        }),
        user->messages[0].tags.emotes
    );
    EXPECT_EQ(0x5B99FF, user->messages[0].tags.color);
    EXPECT_EQ(100, user->messages[0].bits);
}

TEST_F(MessagingTests, ReceiveAction) {
    // Log in and join a channel.
    LogIn();
    Join("foobar1125");

    // Have the pretend Twitch server simulate someone else sending an action
    // chat in the channel.
    mockServer->ReturnToClient(
        ":foobar1126!foobar1126@foobar1126.tmi.twitch.tv PRIVMSG #foobar1125 :" "\x1" "ACTION is testing" "\x1" + CRLF
    );

    // Wait for the message to be received.
    ASSERT_TRUE(user->AwaitMessages(1));
    ASSERT_EQ(1, user->messages.size());
    EXPECT_TRUE(user->messages[0].isAction);
    EXPECT_EQ("foobar1125", user->messages[0].channel);
    EXPECT_EQ("foobar1126", user->messages[0].user);
    EXPECT_EQ(" is testing", user->messages[0].messageContent);
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
    // Just log in (with tags capability).
    // There's no need to join any channel in order to send/receive whispers.
    LogIn(true);

    // Have the pretend Twitch server simulate someone else sending us a
    // whisper.
    mockServer->ReturnToClient(
        "@badges=;color=;display-name=FooBar1126;emotes=;turbo=0;user-id=12345;user-type= "
        ":foobar1126!foobar1126@foobar1126.tmi.twitch.tv WHISPER foobar1124 :Hello, World!" + CRLF
    );

    // Wait for the message to be received.
    ASSERT_TRUE(user->AwaitWhispers(1));
    ASSERT_EQ(1, user->whispers.size());
    EXPECT_EQ("foobar1126", user->whispers[0].user);
    EXPECT_EQ("Hello, World!", user->whispers[0].message);
    EXPECT_EQ(12345, user->whispers[0].tags.userId);
}

TEST_F(MessagingTests, SendWhisper) {
    // Just log in.
    // There's no need to join any channel in order to send/receive whispers.
    LogIn();

    // Send a message to the channel we just joined.
    tmi.SendWhisper("foobar1126", "Hello, World!");
    EXPECT_TRUE(mockServer->AwaitLineReceived("PRIVMSG #jtv :.w foobar1126 Hello, World!"));
}

TEST_F(MessagingTests, ReceiveGenericNoticeGlobal) {
    // Just log in (with tags capability).
    // There's no need to join any channel in order to receive notices.
    LogIn(true);

    // Have the pretend Twitch server simulate some kind of generic notice sent
    // without channel context.
    mockServer->ReturnToClient(
        "@msg-id=fashion :tmi.twitch.tv NOTICE * :Grey is the new black!" + CRLF
    );

    // Wait for the message to be received.
    ASSERT_TRUE(user->AwaitNotices(1));
    ASSERT_EQ(1, user->notices.size());
    EXPECT_EQ("Grey is the new black!", user->notices[0].message);
    EXPECT_EQ("", user->notices[0].channel);
    EXPECT_EQ("fashion", user->notices[0].id);
}

TEST_F(MessagingTests, ReceiveGenericNoticeInChannel) {
    // Log in (with tags capability) and join a channel.
    LogIn(true);
    Join("foobar1125");

    // Have the pretend Twitch server simulate some kind of generic notice sent
    // within the context of a channel.
    mockServer->ReturnToClient(
        "@msg-id=pmi :tmi.twitch.tv NOTICE #foobar1125 :Remember: Positive Mental Attitude!" + CRLF
    );

    // Wait for the message to be received.
    ASSERT_TRUE(user->AwaitNotices(1));
    ASSERT_EQ(1, user->notices.size());
    EXPECT_EQ("Remember: Positive Mental Attitude!", user->notices[0].message);
    EXPECT_EQ("foobar1125", user->notices[0].channel);
    EXPECT_EQ("pmi", user->notices[0].id);
}

TEST_F(MessagingTests, SomeoneElseJoinsChannelWeHaveJoined) {
    // Log in and join a channel.
    LogIn();
    Join("foobar1125");

    // Have the pretend Twitch server simulate someone else joining the chat.
    mockServer->ReturnToClient(
        ":foobar1126!foobar1126@foobar1126.tmi.twitch.tv JOIN #foobar1125" + CRLF
    );

    // Wait for the join to be received.
    // NOTE: first join was us, second join was the other person.
    ASSERT_TRUE(user->AwaitJoins(2));
    ASSERT_EQ(2, user->joins.size());
    EXPECT_EQ("foobar1125", user->joins[1].channel);
    EXPECT_EQ("foobar1126", user->joins[1].user);
}

TEST_F(MessagingTests, SomeoneElseLeavesChannelWeHaveJoined) {
    // Log in and join a channel.
    LogIn();
    Join("foobar1125");

    // Have the pretend Twitch server simulate someone else leaving the chat.
    mockServer->ReturnToClient(
        ":foobar1126!foobar1126@foobar1126.tmi.twitch.tv PART #foobar1125" + CRLF
    );

    // Wait for the join to be received.
    ASSERT_TRUE(user->AwaitLeaves(1));
    ASSERT_EQ(1, user->parts.size());
    EXPECT_EQ("foobar1125", user->parts[0].channel);
    EXPECT_EQ("foobar1126", user->parts[0].user);
}

TEST_F(MessagingTests, AnonymousConnection) {
    // Log in anonymously.
    tmi.LogInAnonymously();
    EXPECT_TRUE(mockServer->AwaitCapLs());
    EXPECT_EQ("302", mockServer->capLsArg);
    EXPECT_FALSE(mockServer->AwaitCapEnd());
    mockServer->ReturnToClient(
        ":tmi.twitch.tv CAP * LS :twitch.tv/membership twitch.tv/tags twitch.tv/commands" + CRLF
    );
    EXPECT_TRUE(mockServer->AwaitCapReq());
    EXPECT_EQ(
        "twitch.tv/commands twitch.tv/membership twitch.tv/tags",
        mockServer->capsRequested
    );
    EXPECT_FALSE(mockServer->AwaitCapEnd());
    mockServer->ReturnToClient(
        ":tmi.twitch.tv CAP * ACK :twitch.tv/commands twitch.tv/membership twitch.tv/tags" + CRLF
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
    EXPECT_FALSE(mockServer->WasPasswordOffered());
    const auto nickname = mockServer->GetNicknameOffered();
    EXPECT_TRUE(std::regex_match(nickname, ANONYMOUS_NICKNAME_PATTERN));
    intmax_t scratch;
    EXPECT_EQ(
        SystemAbstractions::ToIntegerResult::Success,
        SystemAbstractions::ToInteger(nickname.substr(9), scratch)
    );
    EXPECT_FALSE(mockServer->IsDisconnected());

    // Join a channel, but don't expect a Join callback, since it could be
    // confused by the app as another user with a name starting with
    // "justinfan" joining the channel.
    Join("foobar1125", nickname);
    EXPECT_FALSE(user->AwaitJoins(1));

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
    EXPECT_EQ("Hello, World!", user->messages[0].messageContent);

    // Send a message to the channel we just joined.
    mockServer->ClearLinesReceived();
    tmi.SendMessage("foobar1125", "Hello, World!");
    tmi.SendWhisper("foobar1125", "HeyGuys");
    EXPECT_EQ(
        (std::vector< std::string >{
        }),
        mockServer->GetLinesReceived()
    );

    // Leave the channel and verify no Leave callback is triggered.
    Leave("foobar1125", nickname);
    EXPECT_FALSE(user->AwaitLeaves(1));
}

TEST_F(MessagingTests, ChannelStartsHostingSomeoneElse) {
    // Log in and join a channel.
    LogIn();
    Join("foobar1125");

    // Have the pretend Twitch server simulate the condition where the channel
    // begins hosting another channel.
    mockServer->ReturnToClient(
        ":tmi.twitch.tv HOSTTARGET #foobar1125 :foobar1126 42" + CRLF
    );

    // Wait to be notified about a hosting change.
    ASSERT_TRUE(user->AwaitHosts(1));
    ASSERT_EQ(1, user->hosts.size());
    EXPECT_TRUE(user->hosts[0].on);
    EXPECT_EQ("foobar1125", user->hosts[0].hosting);
    EXPECT_EQ("foobar1126", user->hosts[0].beingHosted);
    EXPECT_EQ(42, user->hosts[0].viewers);
}

TEST_F(MessagingTests, ChannelStopsHosting) {
    // Log in and join a channel.
    LogIn();
    Join("foobar1125");

    // Have the pretend Twitch server simulate the condition where the channel
    // begins hosting another channel.
    mockServer->ReturnToClient(
        ":tmi.twitch.tv HOSTTARGET #foobar1125 :- 0" + CRLF
    );

    // Wait to be notified about a hosting change.  NOTE: By not checking
    // "beingHosted", we're saying here that "beingHosted" is irrelevant and
    // there's no need to even look at it, because "on" was false (meaning host
    // mode if off and nobody is being hosted).
    ASSERT_TRUE(user->AwaitHosts(1));
    ASSERT_EQ(1, user->hosts.size());
    EXPECT_FALSE(user->hosts[0].on);
    EXPECT_EQ("foobar1125", user->hosts[0].hosting);
    EXPECT_EQ(0, user->hosts[0].viewers);
}

TEST_F(MessagingTests, RoomModes) {
    // Log in (with tags capability) and join a channel.
    LogIn(true);
    Join("foobar1125");

    // Define the different mode change commands to try.
    struct RoomModeTest {
        std::string description;
        std::string input;
        std::string mode;
        int parameter;
    };
    static const std::vector< RoomModeTest > roomModeTests{
        {
            "Slow mode on for 120 seconds",
            "@room-id=12345;slow=120 :tmi.twitch.tv ROOMSTATE #foobar1125",
            "slow",
            120
        },
        {
            "Slow mode off",
            "@room-id=12345;slow=0 :tmi.twitch.tv ROOMSTATE #foobar1125",
            "slow",
            0
        },
        {
            "Followers-only mode on for 30 minutes",
            "@room-id=12345;followers-only=30 :tmi.twitch.tv ROOMSTATE #foobar1125",
            "followers-only",
            30
        },
        {
            "Followers-only mode off",
            "@room-id=12345;followers-only=-1 :tmi.twitch.tv ROOMSTATE #foobar1125",
            "followers-only",
            -1
        },
        {
            "r9k mode on",
            "@room-id=12345;r9k=1 :tmi.twitch.tv ROOMSTATE #foobar1125",
            "r9k",
            1
        },
        {
            "r9k mode off",
            "@room-id=12345;r9k=0 :tmi.twitch.tv ROOMSTATE #foobar1125",
            "r9k",
            0
        },
        {
            "emote-only mode on",
            "@room-id=12345;emote-only=1 :tmi.twitch.tv ROOMSTATE #foobar1125",
            "emote-only",
            1
        },
        {
            "emote-only mode off",
            "@room-id=12345;emote-only=0 :tmi.twitch.tv ROOMSTATE #foobar1125",
            "emote-only",
            0
        },
        {
            "subs-only mode on",
            "@room-id=12345;subs-only=1 :tmi.twitch.tv ROOMSTATE #foobar1125",
            "subs-only",
            1
        },
        {
            "subs-only mode off",
            "@room-id=12345;subs-only=0 :tmi.twitch.tv ROOMSTATE #foobar1125",
            "subs-only",
            0
        },
    };

    size_t roomModeChanges = 0;
    for (const auto& roomModeTest: roomModeTests) {
        // Have the pretend Twitch server simulate the condition where the room
        // mode changes.
        mockServer->ReturnToClient(roomModeTest.input + CRLF);

        // Wait to be notified about a room mode change.
        ASSERT_TRUE(user->AwaitRoomModeChanges(roomModeChanges + 1)) << roomModeTest.description;
        ASSERT_EQ(roomModeChanges + 1, user->roomModeChanges.size());
        EXPECT_EQ(roomModeTest.mode, user->roomModeChanges[roomModeChanges].mode);
        EXPECT_EQ(roomModeTest.parameter, user->roomModeChanges[roomModeChanges].parameter);
        EXPECT_EQ(12345, user->roomModeChanges[roomModeChanges].channelId);
        EXPECT_EQ("foobar1125", user->roomModeChanges[roomModeChanges].channelName);
        ++roomModeChanges;
    }
}

TEST_F(MessagingTests, TimeoutUser) {
    // Log in (with tags capability) and join a channel.
    LogIn(true);
    Join("foobar1125");

    // Have the pretend Twitch server simulate the condition where someone is
    // timed out by a moderator.
    mockServer->ReturnToClient(
        "@ban-duration=1;ban-reason=Not\\sfunny;room-id=12345;target-user-id=1122334455;tmi-sent-ts=1539652354185 "
        ":tmi.twitch.tv CLEARCHAT #foobar1125 :foobar1126" + CRLF
    );

    // Wait to be notified about the clear.
    ASSERT_TRUE(user->AwaitClears(1));
    ASSERT_EQ(1, user->clears.size());
    EXPECT_EQ(Twitch::Messaging::ClearInfo::Type::Timeout, user->clears[0].type);
    EXPECT_EQ("foobar1125", user->clears[0].channel);
    EXPECT_EQ("foobar1126", user->clears[0].user);
    EXPECT_EQ("Not funny", user->clears[0].reason);
    EXPECT_EQ(1, user->clears[0].duration);
    EXPECT_EQ(1122334455, user->clears[0].tags.userId);
    EXPECT_EQ(12345, user->clears[0].tags.channelId);
    EXPECT_EQ(1539652354, user->clears[0].tags.timestamp);
    EXPECT_EQ(185, user->clears[0].tags.timeMilliseconds);
}

TEST_F(MessagingTests, TimeoutUserWithSpecialCharactersInReason) {
    // Log in (with tags capability) and join a channel.
    LogIn(true);
    Join("foobar1125");

    // Have the pretend Twitch server simulate the condition where someone is
    // timed out by a moderator.
    mockServer->ReturnToClient(
        "@ban-duration=1;ban-reason=just\\sa\\stest:\\sthis=test\\:\\sbackslash:\\s\\\\\\s\\sdouble:\\s\\\\\\\\\\shello,\\sworld!;room-id=12345;target-user-id=1122334455;tmi-sent-ts=1539652354185 "
        ":tmi.twitch.tv CLEARCHAT #foobar1125 :foobar1126" + CRLF
    );

    // Wait to be notified about the clear.
    ASSERT_TRUE(user->AwaitClears(1));
    ASSERT_EQ(1, user->clears.size());
    EXPECT_EQ("just a test: this=test; backslash: \\  double: \\\\ hello, world!", user->clears[0].reason);
}

TEST_F(MessagingTests, BanUser) {
    // Log in (with tags capability) and join a channel.
    LogIn(true);
    Join("foobar1125");

    // Have the pretend Twitch server simulate the condition where someone is
    // timed out by a moderator.
    mockServer->ReturnToClient(
        "@ban-reason=Was\\sa\\sdick;room-id=12345;target-user-id=1122334455;tmi-sent-ts=1539652354185 "
        ":tmi.twitch.tv CLEARCHAT #foobar1125 :foobar1126" + CRLF
    );

    // Wait to be notified about the clear.
    ASSERT_TRUE(user->AwaitClears(1));
    ASSERT_EQ(1, user->clears.size());
    EXPECT_EQ(Twitch::Messaging::ClearInfo::Type::Ban, user->clears[0].type);
    EXPECT_EQ("foobar1125", user->clears[0].channel);
    EXPECT_EQ("foobar1126", user->clears[0].user);
    EXPECT_EQ("Was a dick", user->clears[0].reason);
    EXPECT_EQ(1122334455, user->clears[0].tags.userId);
    EXPECT_EQ(12345, user->clears[0].tags.channelId);
    EXPECT_EQ(1539652354, user->clears[0].tags.timestamp);
    EXPECT_EQ(185, user->clears[0].tags.timeMilliseconds);
}

TEST_F(MessagingTests, ClearAll) {
    // Log in (with tags capability) and join a channel.
    LogIn(true);
    Join("foobar1125");

    // Have the pretend Twitch server simulate the condition where someone is
    // timed out by a moderator.
    mockServer->ReturnToClient(
        "@room-id=12345;tmi-sent-ts=1539652354185 "
        ":tmi.twitch.tv CLEARCHAT #foobar1125" + CRLF
    );

    // Wait to be notified about the clear.
    ASSERT_TRUE(user->AwaitClears(1));
    ASSERT_EQ(1, user->clears.size());
    EXPECT_EQ(Twitch::Messaging::ClearInfo::Type::ClearAll, user->clears[0].type);
    EXPECT_EQ("foobar1125", user->clears[0].channel);
    EXPECT_EQ(12345, user->clears[0].tags.channelId);
    EXPECT_EQ(1539652354, user->clears[0].tags.timestamp);
    EXPECT_EQ(185, user->clears[0].tags.timeMilliseconds);
}

TEST_F(MessagingTests, ClearMessage) {
    // Log in (with tags capability) and join a channel.
    LogIn(true);
    Join("foobar1125");

    // Have the pretend Twitch server simulate the condition where a moderator
    // has deleted an individual message.
    mockServer->ReturnToClient(
        "@login=foobar1126;target-msg-id=11223344-5566-7788-1122-112233445566 "
        ":tmi.twitch.tv CLEARMSG #foobar1125 :Don't ban me, bro!" + CRLF
    );

    // Wait to be notified about the clear.
    ASSERT_TRUE(user->AwaitClears(1));
    ASSERT_EQ(1, user->clears.size());
    EXPECT_EQ(Twitch::Messaging::ClearInfo::Type::ClearMessage, user->clears[0].type);
    EXPECT_EQ("foobar1125", user->clears[0].channel);
    EXPECT_EQ("foobar1126", user->clears[0].user);
    EXPECT_EQ("Don't ban me, bro!", user->clears[0].offendingMessageContent);
    EXPECT_EQ("11223344-5566-7788-1122-112233445566", user->clears[0].offendingMessageId);
}

TEST_F(MessagingTests, UserModded) {
    // Log in and join a channel.
    LogIn();
    Join("foobar1125");

    // Have the pretend Twitch server simulate the condition where a user
    // becomes a moderator.
    mockServer->ReturnToClient(
        ":jtv MODE #foobar1125 +o foobar1126" + CRLF
    );

    // Wait to be notified about the mod event.
    ASSERT_TRUE(user->AwaitMods(1));
    ASSERT_EQ(1, user->mods.size());
    EXPECT_TRUE(user->mods[0].mod);
    EXPECT_EQ("foobar1125", user->mods[0].channel);
    EXPECT_EQ("foobar1126", user->mods[0].user);
}

TEST_F(MessagingTests, UserUnmodded) {
    // Log in and join a channel.
    LogIn();
    Join("foobar1125");

    // Have the pretend Twitch server simulate the condition where a user
    // is no longer a moderator.
    mockServer->ReturnToClient(
        ":jtv MODE #foobar1125 -o foobar1126" + CRLF
    );

    // Wait to be notified about the mod event.
    ASSERT_TRUE(user->AwaitMods(1));
    ASSERT_EQ(1, user->mods.size());
    EXPECT_FALSE(user->mods[0].mod);
    EXPECT_EQ("foobar1125", user->mods[0].channel);
    EXPECT_EQ("foobar1126", user->mods[0].user);
}

TEST_F(MessagingTests, GlobalUserState) {
    // Log in (with tags capability).  No need to join a channel, because this
    // state applies to you globally.
    LogIn(true);

    // Have the pretend Twitch server send the user their global state.
    mockServer->ReturnToClient(
        "@badges=;color=;display-name=FooBar1124;emote-sets=0;user-id=12345;user-type= "
        ":tmi.twitch.tv GLOBALUSERSTATE" + CRLF
    );

    // Wait to be notified about the state event.
    ASSERT_TRUE(user->AwaitUserState(1));
    ASSERT_EQ(1, user->userStates.size());
    EXPECT_TRUE(user->userStates[0].global);
    EXPECT_EQ(12345, user->userStates[0].tags.userId);
    EXPECT_EQ("FooBar1124", user->userStates[0].tags.displayName);
    EXPECT_EQ(
        (std::set< std::string >{
        }),
        user->userStates[0].tags.badges
    );
    EXPECT_EQ(0xFFFFFF, user->userStates[0].tags.color);
}

TEST_F(MessagingTests, ChannelUserState) {
    // Log in (with tags capability) and join a channel.
    LogIn(true);
    Join("foobar1125");

    // Have the pretend Twitch server send the user their channel-specific
    // state.
    mockServer->ReturnToClient(
        "@badges=;color=;display-name=FooBar1124;emote-sets=0;mod=0;subscriber=0;user-type= "
        ":tmi.twitch.tv USERSTATE #foobar1124" + CRLF
    );

    // Wait to be notified about the state event.
    ASSERT_TRUE(user->AwaitUserState(1));
    ASSERT_EQ(1, user->userStates.size());
    EXPECT_FALSE(user->userStates[0].global);
    EXPECT_EQ("FooBar1124", user->userStates[0].tags.displayName);
    EXPECT_EQ(
        (std::set< std::string >{
        }),
        user->userStates[0].tags.badges
    );
    EXPECT_EQ(0xFFFFFF, user->userStates[0].tags.color);
}

TEST_F(MessagingTests, Reconnect) {
    // Log into chat.  We (probably?) don't need to be in any chat room in
    // order to get told about the server's imminent doom!
    LogIn();

    // Have the pretend Twitch server send the user a reconnection
    // notification.
    mockServer->ReturnToClient(
        ":tmi.twitch.tv RECONNECT" + CRLF
    );

    // Wait to be notified about the doom event.
    ASSERT_TRUE(user->AwaitDoom());
}

TEST_F(MessagingTests, ReceiveSubNotificationResub) {
    // Log in (with tags capability) and join a channel.
    LogIn(true);
    Join("foobar1125");

    // Have the pretend Twitch server simulate someone else re-subscribing to
    // the channel.
    mockServer->ReturnToClient(
        // tags
        "@badges=subscriber/3;"
        "color=#008000;"
        "display-name=FooBar1126;"
        "emotes=;"
        "flags=;"
        "id=11223344-5566-7788-1122-112233445566;"
        "login=foobar1126;"
        "mod=0;"
        "msg-id=resub;"
        "msg-param-months=4;"
        "msg-param-sub-plan-name=The\\sPogChamp\\sPlan;"
        "msg-param-sub-plan=1000;"
        "room-id=12345;"
        "subscriber=1;"
        "system-msg=foobar1126\\sjust\\ssubscribed\\swith\\sa\\sTier\\s1\\ssub.\\sfoobar1126\\ssubscribed\\sfor\\s4\\smonths\\sin\\sa\\srow!;"
        "tmi-sent-ts=1539652354185;"
        "turbo=0;"
        "user-id=1122334455;"
        "user-type= "

        // prefix
        ":tmi.twitch.tv "

        // command
        "USERNOTICE "

        // arguments
        "#foobar1125 :Is this all I get for subbing to your channel?  FeelsBadMan" + CRLF
    );

    // Wait for the message to be received.
    ASSERT_TRUE(user->AwaitSubs(1));
    ASSERT_EQ(1, user->subs.size());
    EXPECT_EQ("foobar1125", user->subs[0].channel);
    EXPECT_EQ("foobar1126", user->subs[0].user);
    EXPECT_EQ("Is this all I get for subbing to your channel?  FeelsBadMan", user->subs[0].userMessage);
    EXPECT_EQ("foobar1126 just subscribed with a Tier 1 sub. foobar1126 subscribed for 4 months in a row!", user->subs[0].systemMessage);
    EXPECT_EQ(Twitch::Messaging::SubInfo::Type::Resub, user->subs[0].type);
    EXPECT_EQ("The PogChamp Plan", user->subs[0].planName);
    EXPECT_EQ(4, user->subs[0].months);
    EXPECT_EQ(1000, user->subs[0].planId);
    EXPECT_EQ(1122334455, user->subs[0].tags.userId);
    EXPECT_EQ(12345, user->subs[0].tags.channelId);
    EXPECT_EQ(1539652354, user->subs[0].tags.timestamp);
    EXPECT_EQ(185, user->subs[0].tags.timeMilliseconds);
    EXPECT_EQ("FooBar1126", user->subs[0].tags.displayName);
    EXPECT_EQ(
        (std::set< std::string >{
            "subscriber/3",
        }),
        user->subs[0].tags.badges
    );
    EXPECT_EQ(0x008000, user->subs[0].tags.color);
}

TEST_F(MessagingTests, ReceiveSubNotificationNewSub) {
    // Log in (with tags capability) and join a channel.
    LogIn(true);
    Join("foobar1125");

    // Have the pretend Twitch server simulate someone else subscribing to the
    // channel for the first time, or after not being subscribed for a while.
    mockServer->ReturnToClient(
        // tags
        "@badges=subscriber/3;"
        "color=#008000;"
        "display-name=FooBar1126;"
        "emotes=;"
        "flags=;"
        "id=11223344-5566-7788-1122-112233445566;"
        "login=foobar1126;"
        "mod=0;"
        "msg-id=sub;"
        "msg-param-sub-plan-name=The\\sPogChamp\\sPlan;"
        "msg-param-sub-plan=1000;"
        "room-id=12345;"
        "subscriber=1;"
        "system-msg=foobar1126\\sjust\\ssubscribed\\swith\\sa\\sTier\\s1\\ssub.\\sfoobar1126\\ssubscribed\\sfor\\s4\\smonths\\sin\\sa\\srow!;"
        "tmi-sent-ts=1539652354185;"
        "turbo=0;"
        "user-id=1122334455;"
        "user-type= "

        // prefix
        ":tmi.twitch.tv "

        // command
        "USERNOTICE "

        // arguments
        "#foobar1125 :Is this all I get for subbing to your channel?  FeelsBadMan" + CRLF
    );

    // Wait for the message to be received.
    ASSERT_TRUE(user->AwaitSubs(1));
    ASSERT_EQ(1, user->subs.size());
    EXPECT_EQ("foobar1125", user->subs[0].channel);
    EXPECT_EQ("foobar1126", user->subs[0].user);
    EXPECT_EQ("Is this all I get for subbing to your channel?  FeelsBadMan", user->subs[0].userMessage);
    EXPECT_EQ("foobar1126 just subscribed with a Tier 1 sub. foobar1126 subscribed for 4 months in a row!", user->subs[0].systemMessage);
    EXPECT_EQ(Twitch::Messaging::SubInfo::Type::Sub, user->subs[0].type);
    EXPECT_EQ("The PogChamp Plan", user->subs[0].planName);
    EXPECT_EQ(1000, user->subs[0].planId);
    EXPECT_EQ(1122334455, user->subs[0].tags.userId);
    EXPECT_EQ(12345, user->subs[0].tags.channelId);
    EXPECT_EQ(1539652354, user->subs[0].tags.timestamp);
    EXPECT_EQ(185, user->subs[0].tags.timeMilliseconds);
    EXPECT_EQ("FooBar1126", user->subs[0].tags.displayName);
    EXPECT_EQ(
        (std::set< std::string >{
            "subscriber/3",
        }),
        user->subs[0].tags.badges
    );
    EXPECT_EQ(0x008000, user->subs[0].tags.color);
}

TEST_F(MessagingTests, ReceiveSubNotificationGifted) {
    // Log in (with tags capability) and join a channel.
    LogIn(true);
    Join("foobar1125");

    // Have the pretend Twitch server simulate someone else gifting a
    // subscription to another user.
    mockServer->ReturnToClient(
        // tags
        "@badges=subscriber/3;"
        "color=#008000;"
        "display-name=FooBar1126;"
        "emotes=;"
        "flags=;"
        "id=11223344-5566-7788-1122-112233445566;"
        "login=foobar1126;"
        "mod=0;"
        "msg-id=subgift;"
        "msg-param-recipient-display-name=FooBar1124;"
        "msg-param-recipient-id=5544332211;"
        "msg-param-recipient-user-name=foobar1124;"
        "msg-param-sender-count=3;"
        "msg-param-sub-plan-name=The\\sPogChamp\\sPlan;"
        "msg-param-sub-plan=1000;"
        "room-id=12345;"
        "subscriber=1;"
        "system-msg=foobar1126\\sgifted\\sa\\sTier\\s1\\ssub\\sto\\sFooBar1124!\\sThey\\shave\\sgiven\\s3\\sGift\\sSubs\\sin\\sthe\\schannel!;"
        "tmi-sent-ts=1539652354185;"
        "turbo=0;"
        "user-id=1122334455;"
        "user-type= "

        // prefix
        ":tmi.twitch.tv "

        // command
        "USERNOTICE "

        // arguments
        "#foobar1125" + CRLF
    );

    // Wait for the message to be received.
    ASSERT_TRUE(user->AwaitSubs(1));
    ASSERT_EQ(1, user->subs.size());
    EXPECT_EQ("foobar1125", user->subs[0].channel);
    EXPECT_EQ("foobar1126", user->subs[0].user);
    EXPECT_EQ("", user->subs[0].userMessage);
    EXPECT_EQ("foobar1126 gifted a Tier 1 sub to FooBar1124! They have given 3 Gift Subs in the channel!", user->subs[0].systemMessage);
    EXPECT_EQ(Twitch::Messaging::SubInfo::Type::Gifted, user->subs[0].type);
    EXPECT_EQ("FooBar1124", user->subs[0].recipientDisplayName);
    EXPECT_EQ("foobar1124", user->subs[0].recipientUserName);
    EXPECT_EQ(5544332211, user->subs[0].recipientId);
    EXPECT_EQ(3, user->subs[0].senderCount);
    EXPECT_EQ("The PogChamp Plan", user->subs[0].planName);
    EXPECT_EQ(1000, user->subs[0].planId);
    EXPECT_EQ(1122334455, user->subs[0].tags.userId);
    EXPECT_EQ(12345, user->subs[0].tags.channelId);
    EXPECT_EQ(1539652354, user->subs[0].tags.timestamp);
    EXPECT_EQ(185, user->subs[0].tags.timeMilliseconds);
    EXPECT_EQ("FooBar1126", user->subs[0].tags.displayName);
    EXPECT_EQ(
        (std::set< std::string >{
            "subscriber/3",
        }),
        user->subs[0].tags.badges
    );
    EXPECT_EQ(0x008000, user->subs[0].tags.color);
}

TEST_F(MessagingTests, ReceiveSubNotificationMysteryGift) {
    // Log in (with tags capability) and join a channel.
    LogIn(true);
    Join("foobar1125");

    // Have the pretend Twitch server simulate someone else gifting 3
    // subscriptions to the community of the channel.
    mockServer->ReturnToClient(
        // tags
        "@badges=subscriber/3;"
        "color=#008000;"
        "display-name=FooBar1126;"
        "emotes=;"
        "flags=;"
        "id=11223344-5566-7788-1122-112233445566;"
        "login=foobar1126;"
        "mod=0;"
        "msg-id=submysterygift;"
        "msg-param-mass-gift-count=3;"
        "msg-param-sender-count=15;"
        "msg-param-sub-plan-name=The\\sPogChamp\\sPlan;"
        "msg-param-sub-plan=1000;"
        "room-id=12345;"
        "subscriber=1;"
        "system-msg=foobar1126\\sis\\sgifting\\s3\\sTier\\s1\\sSubs\\sto\\sFooBar1124's\\scommunity!\\sThey've\\sgifted\\sa\\stotal\\sof\\s15\\sin\\sthe\\schannel!;"
        "tmi-sent-ts=1539652354185;"
        "turbo=0;"
        "user-id=1122334455;"
        "user-type= "

        // prefix
        ":tmi.twitch.tv "

        // command
        "USERNOTICE "

        // arguments
        "#foobar1125" + CRLF
    );

    // Wait for the message to be received.
    ASSERT_TRUE(user->AwaitSubs(1));
    ASSERT_EQ(1, user->subs.size());
    EXPECT_EQ("foobar1125", user->subs[0].channel);
    EXPECT_EQ("foobar1126", user->subs[0].user);
    EXPECT_EQ("", user->subs[0].userMessage);
    EXPECT_EQ("foobar1126 is gifting 3 Tier 1 Subs to FooBar1124's community! They've gifted a total of 15 in the channel!", user->subs[0].systemMessage);
    EXPECT_EQ(Twitch::Messaging::SubInfo::Type::MysteryGift, user->subs[0].type);
    EXPECT_EQ(3, user->subs[0].massGiftCount);
    EXPECT_EQ(15, user->subs[0].senderCount);
    EXPECT_EQ("The PogChamp Plan", user->subs[0].planName);
    EXPECT_EQ(1000, user->subs[0].planId);
    EXPECT_EQ(1122334455, user->subs[0].tags.userId);
    EXPECT_EQ(12345, user->subs[0].tags.channelId);
    EXPECT_EQ(1539652354, user->subs[0].tags.timestamp);
    EXPECT_EQ(185, user->subs[0].tags.timeMilliseconds);
    EXPECT_EQ("FooBar1126", user->subs[0].tags.displayName);
    EXPECT_EQ(
        (std::set< std::string >{
            "subscriber/3",
        }),
        user->subs[0].tags.badges
    );
    EXPECT_EQ(0x008000, user->subs[0].tags.color);
}

TEST_F(MessagingTests, ReceiveRaidNotification) {
    // Log in (with tags capability) and join a channel.
    LogIn(true);
    Join("foobar1125");

    // Have the pretend Twitch server simulate someone else raiding the
    // channel.
    mockServer->ReturnToClient(
        // tags
        "@badges=subscriber/3;"
        "color=#008000;"
        "display-name=FooBar1126;"
        "emotes=;"
        "flags=;"
        "id=11223344-5566-7788-1122-112233445566;"
        "login=foobar1126;"
        "mod=0;"
        "msg-id=raid;"
        "msg-param-displayName=FooBar1126;"
        "msg-param-login=foobar1126;"
        "msg-param-profileImageURL=http://www.example.com/icon.jpg;"
        "msg-param-viewerCount=1234;"
        "room-id=12345;"
        "subscriber=1;"
        "system-msg=1234\\sraiders\\sfrom\\sFooBar1126\\shave\\sjoined\\n!;"
        "tmi-sent-ts=1539652354185;"
        "turbo=0;"
        "user-id=1122334455;"
        "user-type= "

        // prefix
        ":tmi.twitch.tv "

        // command
        "USERNOTICE "

        // arguments
        "#foobar1125" + CRLF
    );

    // Wait for the message to be received.
    ASSERT_TRUE(user->AwaitRaids(1));
    ASSERT_EQ(1, user->raids.size());
    EXPECT_EQ("foobar1125", user->raids[0].channel);
    EXPECT_EQ("foobar1126", user->raids[0].raider);
    EXPECT_EQ(1234, user->raids[0].viewers);
    EXPECT_EQ("1234 raiders from FooBar1126 have joined\n!", user->raids[0].systemMessage);
    EXPECT_EQ(1122334455, user->raids[0].tags.userId);
    EXPECT_EQ(12345, user->raids[0].tags.channelId);
    EXPECT_EQ(1539652354, user->raids[0].tags.timestamp);
    EXPECT_EQ(185, user->raids[0].tags.timeMilliseconds);
    EXPECT_EQ("FooBar1126", user->raids[0].tags.displayName);
    EXPECT_EQ(
        (std::set< std::string >{
            "subscriber/3",
        }),
        user->raids[0].tags.badges
    );
    EXPECT_EQ(0x008000, user->raids[0].tags.color);
}

TEST_F(MessagingTests, ReceiveRitualNotification) {
    // Log in (with tags capability) and join a channel.
    LogIn(true);
    Join("foobar1125");

    // Have the pretend Twitch server simulate a ritual notification.
    mockServer->ReturnToClient(
        // tags
        "@badges=premium/1;"
        "color=#008000;"
        "display-name=FooBar1126;"
        "emotes=30259:0-6;"
        "flags=;"
        "id=11223344-5566-7788-1122-112233445566;"
        "login=foobar1126;"
        "mod=0;"
        "msg-id=ritual;"
        "msg-param-ritual-name=new_chatter;"
        "room-id=12345;"
        "subscriber=1;"
        "system-msg=@foobar1126\\sis\\snew\\shere.\\sSay\\shello!;"
        "tmi-sent-ts=1539652354185;"
        "turbo=0;"
        "user-id=1122334455;"
        "user-type= "

        // prefix
        ":tmi.twitch.tv "

        // command
        "USERNOTICE "

        // arguments
        "#foobar1125 :HeyGuys" + CRLF
    );

    // Wait for the message to be received.
    ASSERT_TRUE(user->AwaitRituals(1));
    ASSERT_EQ(1, user->rituals.size());
    EXPECT_EQ("foobar1125", user->rituals[0].channel);
    EXPECT_EQ("foobar1126", user->rituals[0].user);
    EXPECT_EQ("new_chatter", user->rituals[0].ritual);
    EXPECT_EQ("@foobar1126 is new here. Say hello!", user->rituals[0].systemMessage);
    EXPECT_EQ(1122334455, user->rituals[0].tags.userId);
    EXPECT_EQ(12345, user->rituals[0].tags.channelId);
    EXPECT_EQ(1539652354, user->rituals[0].tags.timestamp);
    EXPECT_EQ(185, user->rituals[0].tags.timeMilliseconds);
    EXPECT_EQ("FooBar1126", user->rituals[0].tags.displayName);
    EXPECT_EQ(
        (std::set< std::string >{
            "premium/1",
        }),
        user->rituals[0].tags.badges
    );
    EXPECT_EQ(0x008000, user->rituals[0].tags.color);
}

TEST_F(MessagingTests, ReceivePrivateMessage) {
    // Log in and join our own channel.
    LogIn();
    Join("foobar1124");

    // Have the pretend Twitch server simulate telling us that we are now being
    // hosted by someone else.
    mockServer->ReturnToClient(
        ":jtv!jtv@jtv.tmi.twitch.tv PRIVMSG foobar1124 :foobar1126 is now hosting you." + CRLF
    );

    // Wait for the message to be received.
    ASSERT_TRUE(user->AwaitPrivateMessages(1));
    ASSERT_EQ(1, user->privateMessages.size());
    EXPECT_EQ("jtv", user->privateMessages[0].user);
    EXPECT_EQ("foobar1126 is now hosting you.", user->privateMessages[0].messageContent);
}
