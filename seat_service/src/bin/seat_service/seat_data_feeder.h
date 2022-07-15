/********************************************************************************
* Copyright (c) 2022 Contributors to the Eclipse Foundation
*
* See the NOTICE file(s) distributed with this work for additional
* information regarding copyright ownership.
*
* This program and the accompanying materials are made available under the
* terms of the Apache License 2.0 which is available at
* http://www.apache.org/licenses/LICENSE-2.0
*
* SPDX-License-Identifier: Apache-2.0
********************************************************************************/
/**
 * @file      seat_data_feeder.h
 * @brief     The seat data feeder is responsible to feed the seat-related data
 *            points into the KUKSA Data Broker:
 *             * It defines the metadata of those data points.
 *             * It subscribes to the signals provided via the seat service
 *               internal SeatAdjuster interface for receiving updates of those.
 *             * It uses the generic class DataBrokerFeeder to foward the updates
 *               to the Data Broker.
 */
#pragma once

#include <memory>

namespace sdv {

// fwd decl
class SeatAdjuster;
namespace broker_feeder {
    class DataBrokerFeeder;
}

namespace seat_service {

class SeatDataFeeder {
public:
    SeatDataFeeder(std::shared_ptr<SeatAdjuster>, const std::string& broker_addr);
    /**
     * Starts the feeder trying to connect to the data broker, registering data points
     * and sending data point updates to the broker.
     * Note: This function will block the calling thread until the feeder is terminated by
     * an unrecoverable error or a call to Shutdown() or the destructor. It should typically 
     * run in an own thread created by the caller.
     */
    void Run();
    /** Terminates the running feeder */
    void Shutdown();
private:
    std::shared_ptr<SeatAdjuster> seat_adjuster_;
    std::shared_ptr<broker_feeder::DataBrokerFeeder> broker_feeder_;
};

}  // namespace seat_service
}  // namespace sdv
