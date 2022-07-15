# How to create a vehicle service

- [How to create a vehicle service](#how-to-create-a-vehicle-service)
  - [Motivation](#motivation)
  - [Overview](#overview)
  - [Define a service interface](#define-a-service-interface)
    - [Semantic model](#semantic-model)
    - [Interface technology and programming languages](#interface-technology-and-programming-languages)
    - [Interface guideline](#interface-guideline)
    - [Error codes](#error-codes)
  - [Implement a vehicle service](#implement-a-vehicle-service)
    - [Examples](#examples)
    - [Responsibility](#responsibility)
    - [Generation of the language-specific gRPC stubs](#generation-of-the-language-specific-grpc-stubs)
      - [Compiled languages](#compiled-languages)
      - [Interpreted languages](#interpreted-languages)
    - [Connection status](#connection-status)
  - [Add Dapr support to the vehicle service](#add-dapr-support-to-the-vehicle-service)
    - [Metadata](#metadata)
    - [Connection status with Dapr](#connection-status-with-dapr)
  - [Interaction with the KUKSA Data Broker (optional)](#interaction-with-the-kuksa-data-broker-optional)

## Motivation

Vehicle services abstract complex behavior of the vehicle. They abstract the differences in electric and electronic architecture of vehicles from different brands and models to a common interface, which is aligned to a harmonized semantic model. A _Vehicle App_ developer interacts with the vehicle abstraction layer components either directly via the generated service stubs or via the Velocitas SDK/model. The SDK and the VAL components are aligned via the interface description, which is derived from the semantic model.

## Overview

Following steps are needed to create a vehicle service:

- Define a service interface
- Implement the service
- Add support for [Dapr](https://dapr.io) middleware
- (optional) Implement simulation
- (optional) Interaction with the KUKSA data broker

## Define a service interface

### Semantic model

The service interface of the vehicle service which abstracts the vehicle behavior, should be aligned to the semantic model. The [VEHICLE SERVICE CATALOG](https://github.com/COVESA/vehicle_service_catalog) can be used as the semantic model for a service call.

### Interface technology and programming languages

The interface technology used for remote procedure calls is [gGRPC](https://grpc.io/). The methods and datatypes are first defined in a \*.proto file, which is then used to generate client and server stubs/skeletons in [different languages](https://grpc.io/docs/languages/).

How to use the interface and generate code can be read e.g., here: [Basics tutorial for gRPC in C++](https://grpc.io/docs/languages/cpp/basics/) or [Basics tutorial for gRPC in Python](https://grpc.io/docs/languages/python/basics/).

### Interface guideline

The [gRPC interface guideline](interface_guideline.md) provides you with help on how to specify the service interface.

### Error codes

The error codes and recommended usage is described in [Error Handling](interface_guideline.md#error-handling). The errors implemented should be described in the gRPC interface description files (\*.proto).

## Implement a vehicle service

### Examples

For help on how to implement the vehicle service, see the example seat service or the [gRPC examples](https://github.com/grpc/grpc/tree/master/examples) in different languages.

### Responsibility

A vehicle service ...

- Can provide service interfaces to control actuators or to trigger (complex) actions
- Can provide service interfaces to get data
- Uses service interfaces to register and publish data to the data broker
- Reconnects to the data broker in case the connection is lost
- Uses the service interface of the data broker via Dapr, if deployed
- Communicates with a vehicle network, which is connected to real hardware (e.g., CAN interface)
- Communicates with a virtual interface (e.g., CAN interface)
- (Optional) Provides a simulation mode to run without a network interface

A vehicle service implementation ..

- might be implemented vehicle- or project-specific

### Generation of the language-specific gRPC stubs

Before you can do the implementation of your service, you need to generate the required gRPC stubs.
Depending on the kind of programming language you will chose different approches to integrate the stub generation into the overall build and deployment process.

#### Compiled languages

If you implement your service with a *compiled* language like C/C++, the stub generation typically becomes a step in the build process.
The generated stubs are typically not checked in to your version control.

The seat service example shows how this approach is working:
* The stubs are automatically generated during the build process.
   This is defined in the cmake files of the [seat interface](https://github.com/eclipse/kuksa.val.services/blob/main/seat_service/src/lib/grpc_services/CMakeLists.txt) and
   the [data broker interfaces](https://github.com/eclipse/kuksa.val.services/blob/main/seat_service/src/lib/broker_feeder/CMakeLists.txt).
* The generated stubs are stored in the `target` subfolder of the seat service folder.

Todo: The generated C++ stubs of the data broker could be downloaded as a library from the data broker repository.
At least, the proto files should be obtained from there.

#### Interpreted languages

If you implement your service with a *interpreted* language like Python or JavaScript, there is no build step and the stub generation must be done before implementing or at least before excecuting or deploying the service code. Here, the stub generation can be handled by a script which is either called manually or automatically (via CI pipeline) after changing the interface. The generated stubs will typically be checked-in to your version control.

The HVAC service example shows how this approach is working:
* The stubs are manually generated by calling the shell script [update-protobuf.sh](https://github.com/eclipse/kuksa.val.services/blob/main/hvac_service/update-protobuf.sh).
* It generates the stubs for the HVAC service and the KUKSA data broker into the [sdv subfolder](https://github.com/eclipse/kuksa.val.services/tree/main/hvac_service/sdv).
* The generated stubs need to be manually checked-in.
  
Todo: This process shall be automated and the generated stubs shall be provided as a Python package downloadable as a release artifact.

### Connection status

To be able to detect connectivity problems users of a gRPC service, e.g., for [feeding data to the data broker](#interaction-with-the-kuksa-data-broker-optional), shall preferably monitor the gRPC connection state, see: [gRPC Connectivity Semantics and API ](https://grpc.github.io/grpc/core/md_doc_connectivity-semantics-and-api.html).
This can be used to report or log errors if the connection behaves differently than expected.

## Add Dapr support to the vehicle service

You can use the gRPC proxying feature of Dapr, which allows you to use custom \*.proto files/ interfaces instead of the predefined Dapr gRPC interfaces. More can be read at [How-To: Invoke services using gRPC](https://docs.dapr.io/developing-applications/building-blocks/service-invocation/howto-invoke-services-grpc/).

To support Dapr within your vehicle service application ..

- the gRPC metadata `dapr-app-id` of the server needs to be specified in the call
- the used gRPC port should be configurable at least via the environment variable, use `DAPR_GRPC_PORT`.

A full reference to the Dapr environment variables can be found [here](https://docs.dapr.io/reference/environment/).

### Metadata

To call the methods of a gRPC server (e.g. register data points on the data broker) via Dapr the gRPC calls need to be extended with metadata.

We recommend that you read the `dapr-app-id` of the gRPC server either from the command line parameter, environment variable of configuration file instead of hardcoding it, see for Python:

```python
    grpc_metadata = (
        ("dapr-app-id", os.environ.get("VEHICLEDATABROKER_DAPR_APP_ID")),
    )

    channel = grpc.insecure_channel(databroker_address)
    channel.subscribe( ... )
    self._provider = databroker.Provider(channel, grpc_metadata)
```

This makes it possible to keep the resulting containerized application independent of the configuration and avoid unnecessary rebuilds.

### Connection status with Dapr

In a Dapr environment, evaluating the gRPC connectivity state as described in [connection status](#connection-status) is not sufficient. Since the vehicle service communicates via gRPC with the Dapr sidecar application instead with the Dapr server directly, the gRPC communication state is also related to the Dapr side car instead of the server. Additional measures need to be taken to check if the "real" server is available.
Dapr gRPC proxying is only allowed AFTER the Dapr sidecar is fully initialized. If an app port has been specified when running the Dapr sidecar, the sidecar verifies that the service is listening on the port first as part of the initialization.

The following sequence in vehicle services is recommended:

1. Create a gRPC Server and listen
2. Wait for the sidecar (e.g., poll health endpoint or sleep )
3. Execute gRPC proxying calls (e.g., against KUKSA Data Broker)

Example: A callback for the connectivity state in conjugation with retry to call the server:

```python
    channel = grpc.insecure_channel(databroker_address)
    channel.subscribe(
        lambda connectivity: self.on_broker_connectivity_change(connectivity),
        try_to_connect=False,
    )

    ...

    def on_broker_connectivity_change(self, connectivity):
        if (
            connectivity == grpc.ChannelConnectivity.READY
            or connectivity == grpc.ChannelConnectivity.IDLE
        ):
            ## Can change between READY and IDLE. Only act if coming from
            ## unconnected state
            if not self._connected:
                log.info("Connected to data broker")
                try:
                    self._register_datapoints()
                    self._registered = True
                except Exception:
                    log.error("Failed to register datapoints", exc_info=True)
                self._connected = True
        else:
            if self._connected:
                log.info("Disconnected from data broker")
            else:
                if connectivity == grpc.ChannelConnectivity.CONNECTING:
                    log.info("Trying to connect to data broker")
            self._connected = False
            self._registered = False
```

If the Dapr side car is ready to forward, communication can be checked with the [GRPC Health Checking Protocol ](https://grpc.github.io/grpc/core/md_doc_health-checking.html), see also [Python gRPC Health Checking](https://grpc.github.io/grpc/python/grpc_health_checking.html).

## Interaction with the KUKSA Data Broker (optional)

Use the [interface description of the KUKSA data broker (\*.proto files)](https://github.com/eclipse/kuksa.val/tree/master/kuksa_databroker/proto) to publish data to the data broker. In the `collector.proto` you find methods to

1.  register datapoints `RegisterDatapoints`, afterwards you can
2.  feed data via single calls `UpdateDatapoints` or in a stream manner `StreamDatapoints`.

See the \*.proto files for a detailed description.
