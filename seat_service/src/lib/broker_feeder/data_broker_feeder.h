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
 * @file      data_broker_feeder.h
 * @brief     The DataBrokerFeeder is generic feeder allowing to register and 
 *            feed data points into the KUKSA Data Broker:
 *             * The set of feedable datapoints is passed on construction time
 *               to the feeder as a parameter together with the broker address.
 *             * Data points can be feed separately or as a batch.
 *             * It handles the registration of the data points (metadata) with
 *               the Data Broker.
 *             * It also handles reconnection to the broker after connection loss
 */
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "sdv/databroker/v1/types.pb.h"
namespace sdv {
namespace broker_feeder {


struct DatapointMetadata {
    std::string name;
    sdv::databroker::v1::DataType data_type;
    sdv::databroker::v1::ChangeType change_type;
    sdv::databroker::v1::Datapoint initial_value;
    std::string description;
};

using DatapointConfiguration = std::vector<DatapointMetadata>;
using DatapointValues = std::unordered_map<std::string, sdv::databroker::v1::Datapoint>;

class DataBrokerFeeder {
public:
    /**
     * Create a new feeder instance
     * 
     * @param broker_addr address of the broker to connect to; format "<ip-address>:<port>"
     * @param dpConfig metadata and initial values of the data points to register
     */
    static std::shared_ptr<DataBrokerFeeder> createInstance(
        const std::string& broker_addr,
        DatapointConfiguration&& dpConfig);
    
    virtual ~DataBrokerFeeder() = default;

    /**
     * Starts the feeder trying to connect to the data broker, registering data points
     * and sending data point updates to the broker.
     * Note: This function will block the calling thread until the feeder is terminated by
     * an unrecoverable error or a call to Shutdown() or the destructor. It should typically 
     * run in an own thread created by the caller.
     */
    virtual void Run() = 0;
    /** Terminates the running feeder */
    virtual void Shutdown() = 0;

    /**
     * Try to feed a single data point value to the broker.
     * The data point must have been part of the dpConfig passed at creation time.
     * @param name Name (path) of the data point to be fed (update).
     * @param value The value to be fed
     */
    virtual void FeedValue(
        const std::string& name,
        const sdv::databroker::v1::Datapoint& value) = 0;

    /**
     * Try to feed a batch of data point values to the broker.
     * The data points must have been part of the dpConfig passed at creation time.
     * @param values A map of data point names (keys) to data point values to be fed
     */
    virtual void FeedValues(const DatapointValues& values) = 0;

protected:
    DataBrokerFeeder() = default;
    DataBrokerFeeder(const DataBrokerFeeder&) = delete;
    DataBrokerFeeder& operator=(const DataBrokerFeeder&) = delete;
};


}  // namespace broker_feeder
}  // namespace sdv
