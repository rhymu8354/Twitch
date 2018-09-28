#ifndef TWITCH_TIME_KEEPER_HPP
#define TWITCH_TIME_KEEPER_HPP

/**
 * @file TimeKeeper.hpp
 *
 * This module declares the Twitch::TimeKeeper interface.
 *
 * © 2018 by Richard Walters
 */

namespace Twitch {

    /**
     * This represents the time-keeping requirements of Twitch classes.
     * To integrate Twitch into a larger program, implement this
     * interface in terms of real time.
     */
    class TimeKeeper {
    public:
        // Methods

        /**
         * This method returns the current server time, in seconds.
         *
         * @return
         *     The current server time is returned, in seconds.
         */
        virtual double GetCurrentTime() = 0;
    };

}

#endif /* TWITCH_TIME_KEEPER_HPP */
