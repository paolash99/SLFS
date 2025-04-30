#pragma once
#ifndef POLICY_ALL_HPP__
#define POLICY_ALL_HPP__

#include "worker-filetoworker.hpp"
#include "worker-filetoworker-lowest-load.hpp"
#include "worker-filetoworker-random.hpp"
#include "worker-filetoworker-active-load-balance.hpp"

#include "worker-keepalive.hpp"
#include "worker-keepalive-const-time.hpp"
#include "worker-keepalive-moving-interval.hpp"
#include "worker-keepalive-moving-interval-global.hpp"

#include "worker-launch.hpp"
#include "worker-launch-const-average-load.hpp"
#include "worker-launch-max-queue.hpp"
#include "worker-launch-single-request.hpp"
#include "worker-launch-fix-pool.hpp"

//#include "worker-launch-const-limit-launch.hpp"
//#include "worker-launch-prestart-one.hpp"
//#include "worker-launch-adaptive-max-load.hpp"


#endif // POLICY_ALL_HPP__
