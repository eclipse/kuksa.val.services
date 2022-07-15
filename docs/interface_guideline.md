# Interface Design Guideline

## GRPC Interface Style Guide

This document provides a style guide for .proto files. By following these conventions, you'll make your protocol buffer message definitions and their corresponding classes consistent and easy to read.
Unless otherwise indicated, this style guide is based on the style guide from [google protocol-buffers style](https://developers.google.com/protocol-buffers/docs/style) under Apache 2.0 License & Creative Commons Attribution 4.0 License.

Note that protocol buffer style can evolve over time, so it is likely that you will see .proto files written in different conventions or styles. Please respect the existing style when you modify these files. Consistency is key. However, it is best to adopt the current best style when you are creating a new .proto file.

### Standard file formatting

- Keep the line length to 80 characters.
- Use an indent of 2 spaces.
- Prefer the use of double quotes for strings.

### File structure

Files should be named lower_snake_case.proto

All files should be ordered in the following manner:

- License header
- File overview
- Syntax
- Package
- Imports (sorted)
- File options
- Everything else

### Directory Structure

Files should be stored in a directory structure that matches their package sub-names. All files
in a given directory should be in the same package.
Below is an example based on the [proto files](https://github.com/eclipse/kuksa.val/tree/master/kuksa_databroker/proto) in the kuksa.val repository.

```text
|   proto/
|   └── sdv
|       └── databroker
|           └── v1                  // package sdv.databroker.broker.v1
|               ├── broker.proto    // service Broker in sdv.databroker.broker.v1
|               ├── collector.proto // service Collector in sdv.databroker.broker.v1
|               └── types.proto     // type definition and import of  in sdv.databroker.broker.v1
```

The proposed structure shown above is adapted from [Uber Protobuf Style Guide V2](https://raw.githubusercontent.com/uber/prototool/dev/style/README.md) under MIT License.

### Packages

Package names should be in lowercase. Package names should have unique names based on the project name, and possibly based on the path of the file containing the protocol buffer type definitions.

### Message and field names

Use PascalCase (CamelCase with an initial capital) for message names – for example, SongServerRequest. Use underscore_separated_names for field names (including oneof field and extension names) – for example, song_name.

```text
message SongServerRequest {
   optional string song_name = 1;
}
```

Using this naming convention for field names gives you accessors like the following:

C++:

```cpp
const string& song_name() { ... }
void set_song_name(const string& x) { ... }
```

If your field name contains a number, the number should appear after the letter instead of after the underscore. For example, use song_name1 instead of song_name_1
Repeated fields

Use pluralized names for repeated fields.

```proto
repeated string keys = 1;
...
repeated MyMessage accounts = 17;
```

### Enums

Use PascalCase (with an initial capital) for enum type names and CAPITALS_WITH_UNDERSCORES for value names:

```cpp
enum FooBar {
   FOO_BAR_UNSPECIFIED = 0;
   FOO_BAR_FIRST_VALUE = 1;
   FOO_BAR_SECOND_VALUE = 2;
}
```

Each enum value should end with a semicolon, not a comma. The zero value enum should have the suffix UNSPECIFIED.

### Services

If your .proto defines an RPC service, you should use PascalCase (with an initial capital) for both the service name and any RPC method names:

```proto
service FooService {
   rpc GetSomething(GetSomethingRequest) returns (GetSomethingResponse);
   rpc ListSomething(ListSomethingRequest) returns (ListSomethingResponse);
}
```

### GRPC Interface Versioning

All API interfaces must provide a major version number, which is encoded at the end of the protobuf package.
If an API introduces a breaking change, such as removing or renaming a field, it must increment its API version number to ensure that existing user code does not suddenly break.
Note: The use of the term "major version number" above is taken from semantic versioning. However, unlike in traditional semantic versioning, APIs must not expose minor or patch version numbers.
For example, APIs use v1, not v1.0, v1.1, or v1.4.2. From a user's perspective, minor versions are updated in place, and users receive new functionality without migration.

A new major version of an API must not depend on a previous major version of the same API. An API may depend on other APIs, with an expectation that the caller understands the dependency and stability risk associated with those APIs. In this scenario, a stable API version must only depend on stable versions of other APIs.

Different versions of the same API should preferably be able to work at the same time within a single client application for a reasonable transition period. This time period allows the client to transition smoothly to the newer version. An older version must go through a reasonable, well-communicated deprecation period before being shut down.

For releases that have alpha or beta stability, APIs must append the stability level after the major version number in the protobuf package.

#### Release-based versioning

An individual release is an alpha or beta release that is expected to be available for a limited time period before its functionality is incorporated into the stable channel, after which the individual release will be shut down.
When using release-based versioning strategy, an API may have any number of individual releases at each stability level.

Alpha and beta releases must have their stability level appended to the version, followed by an incrementing release number. For example, v1beta1 or v1alpha5. APIs should document the chronological order of these versions in their documentation (such as comments).
Each alpha or beta release may be updated in place with backwards-compatible changes. For beta releases, backwards-incompatible updates should be made by incrementing the release number and publishing a new release with the change. For example, if the current version is v1beta1, then v1beta2 is released next.

Adapted from [google release-based_versioning](https://cloud.google.com/apis/design/versioning?hl=en#release-based_versioning) under Apache 2.0 License & Creative Commons Attribution 4.0 License

### Backwards compatibility

The gRPC protocol is designed to support services that change over time. Generally, additions to gRPC services and methods are non-breaking. Non-breaking changes allow existing clients to continue working without changes. Changing or deleting gRPC services are breaking changes. When gRPC services have breaking changes, clients using that service have to be updated and redeployed.

Making non-breaking changes to a service has a number of benefits:

- Existing clients continue to run.
- Avoids work involved with notifying clients of breaking changes, and updating them.
- Only one version of the service needs to be documented and maintained.

### Non-breaking changes

These changes are non-breaking at a gRPC protocol level and binary level.

- Adding a new service
- Adding a new method to a service
- Adding a field to a request message - Fields added to a request message are deserialized with the default value on the server when not set. To be a non-breaking change, the service must succeed when the new field isn't set by older clients.
- Adding a field to a response message - Fields added to a response message are deserialized into the message's unknown fields collection on the client.
- Adding a value to an enum - Enums are serialized as a numeric value. New enum values are deserialized on the client to the enum value without an enum name. To be a non-breaking change, older clients must run correctly when receiving the new enum value.

### Binary breaking changes

The following changes are non-breaking at a gRPC protocol level, but the client needs to be updated if it upgrades to the latest .proto contract. Binary compatibility is important if you plan to publish a gRPC library.

- Removing a field - Values from a removed field are deserialized to a message's unknown fields. This isn't a gRPC protocol breaking change, but the client needs to be updated if it upgrades to the latest contract. It's important that a removed field number isn't accidentally reused in the future. To ensure this doesn't happen, specify deleted field numbers and names on the message using Protobuf's reserved keyword.
- Renaming a message - Message names aren't typically sent on the network, so this isn't a gRPC protocol breaking change. The client will need to be updated if it upgrades to the latest contract. One situation where message names are sent on the network is with Any fields, when the message name is used to identify the message type.
- Nesting or unnesting a message - Message types can be nested. Nesting or unnesting a message changes its message name. Changing how a message type is nested has the same impact on compatibility as renaming.

### Protocol breaking changes

The following items are protocol and binary breaking changes:

- Renaming a field - With Protobuf content, the field names are only used in generated code. The field number is used to identify fields on the network. Renaming a field isn't a protocol breaking change for Protobuf. However, if a server is using JSON content, then renaming a field is a breaking change.
- Changing a field data type - Changing a field's data type to an incompatible type will cause errors when deserializing the message. Even if the new data type is compatible, it's likely the client needs to be updated to support the new type if it upgrades to the latest contract.
- Changing a field number - With Protobuf payloads, the field number is used to identify fields on the network.
- Renaming a package, service or method - gRPC uses the package name, service name, and method name to build the URL. The client gets an UNIMPLEMENTED status from the server.
- Removing a service or method - The client gets an UNIMPLEMENTED status from the server when calling the removed method.

### Behavior breaking changes

When making non-breaking changes, you must also consider whether older clients can continue working with the new service behavior. For example, adding a new field to a request message:

- Isn't a protocol breaking change.
- Returning an error status on the server if the new field isn't set makes it a breaking change for old clients.

Behavior compatibility is determined by your app-specific code.

Adapted from [Versioning gRPC services](https://docs.microsoft.com/en-us/aspnet/core/grpc/versioning?view=aspnetcore-6.0) under Creative Commons Attribution 4.0 License

## Error Handling

### gRPC error handling

In gRPC, a large set of error codes has been [defined](https://grpc.github.io/grpc/cpp/md_doc_statuscodes.html)
As a general rule, SDV should use relevant gRPC error codes,
as described in [this thread](https://stackoverflow.com/questions/59094839/whats-the-correct-way-to-return-a-not-found-response-from-a-grpc-c-server-i)

```cpp
   return grpc::Status(grpc::StatusCode::NOT_FOUND, "error details here");
```

Available constructor:

```cpp
   grpc::Status::Status ( StatusCode  code,
      const std::string & error_message,
      const std::string & error_details
```

The framework for drafting error messages could be useful as a later improvement. This could e.g., be used to specify which unit created the error message and to assure the same structure on all messages. The latter two may e.g., depend on debug settings, e.g., error details only in debug-builds to avoid leaks of sensitive information. A global function like below or similar could handle that and also possibly convert between internal error codes and gRPC codes.

```cpp
   grpc::Status status = CreateStatusMessage(PERMISSION_DENIED,"DataBroker","Rule access rights violated");
```

### VSC error handling

VSC recently added errors to the [example service](https://github.com/COVESA/vehicle_service_catalog/blob/master/comfort-service.yml).

```text
   methods:
      - name: move
        description: |
          Set the desired seat position
        in:
          - name: seat
            description: |
              The desired seat position
            datatype: seat_t

        error:
          datatype: .stdvsc.error_t
          range: $ in_set("ok", "completed", "in_progress", "interrupted")
```

The errors come from a [standardized error list](https://github.com/COVESA/vehicle_service_catalog/blob/master/vsc-error.yml),
and for each method in VSC, a range clause can be used to specify which errors from that list that are applicable.

It is in VSC explicitly [specified](https://github.com/COVESA/vehicle_service_catalog#methods-list-object-error) that:

_Transport-layer issues arising from interrupted communication, services going down, etc, are handled on a language-binding level where each language library implements its own way of detecting, reporting, and recovering from network-related errors._

This means that VSC error messages intentionally cover a smaller subset than gRPC error messages. Unlike gRPC,
there is currently no detailed documentation in VCS on when individual error codes shall be used, which could cause
problems as some error codes are similar.

### SDV error handling for gRPC interfaces (e.g., VAL vehicles services)

- Use gRPC error codes as base
- Document in proto files (as comments) which error codes that the service implementation can emit and the meaning of them. (Errors that only are emitted by the gRPC framework do not need to be listed.)
- Do not - unless there are special reasons - add explicit error/status fields to rpc return messages.
- Additional error information can be given by free text fields in gRPC error codes. Note, however, that sensitive information like `Given password ABCD does not match expected password EFGH` should not be passed in an unprotected/unencrypted manner.

### SDV handling of gRPC error codes

The table below gives error code guidelines for each gRPC on:

- If it is relevant for a client to retry the call or not when receiving the error code. Retry is only relevant if the error is of a temporary nature.
- Which VSC error code matches the respective gRPC error code
- When to use the error code when implementing a service.

<table border="1">
  <tbody>
    <tr>
      <th>gRPC error code</th>
      <th>Retry Relevant?</th>
      <th>Corresponding VSC error codes</th>
      <th>Recommended SDV usage</th>
    </tr>
    <tr>
      <td>OK</td>
      <td>No</td>
      <td>ok, completed, in_progress</td>
      <td>Mandatory error code if operation succeeded. Shall never be used if operation failed.</td>
    </tr>
    <tr>
      <td>CANCELLED</td>
      <td>No</td>
      <td>interrupted</td>
      <td>No explicit use case on server side in SDV identified</td>
    </tr>
    <tr>
      <td>UNKNOWN</td>
      <td>No</td>
      <td>other</td>
      <td>To be used in default-statements when converting errors from e.g., Broker-errors to SDV/gRPC errors</td>
    </tr>
    <tr>
      <td>INVALID_ARGUMENT</td>
      <td>No</td>
      <td>invalid_argument</td>
      <td>E.g., Rule syntax with errors</td>
    </tr>
    <tr>
      <td>DEADLINE_EXCEEDED</td>
      <td>Yes</td>
      <td>expired</td>
      <td>Only applicable for asynchronous services, i.e. services which wait for completion before the result is returned. The behavior if a VSC operation cannot finish within expected time must be defined. Two options exist. One is to return this error after e.g., X seconds. Another is that the server never gives up, but rather waits for the client to cancel the operation.</td>
    </tr>
    <tr>
      <td>NOT_FOUND</td>
      <td>No</td>
      <td>not_found</td>
      <td>Long term situation that likely not will change in the near future. <br/> Example: SDV can not find the specified resource (e.g., no path to get data for specified seat) </td>
    </tr>
    <tr>
      <td>ALREADY_EXISTS</td>
      <td>No</td>
      <td>other</td>
      <td>No explicit use case on server side in SDV identified</td>
    </tr>
    <tr>
      <td>PERMISSION_DENIED</td>
      <td>No</td>
      <td>permission_denied</td>
      <td>Operation rejected due to permission denied</td>
    </tr>
    <tr>
      <td>RESOURCE_EXHAUSTED</td>
      <td>Yes</td>
      <td>no_resource, busy</td>
      <td>Possibly if e.g., malloc fails or similar errors.</td>
    </tr>
    <tr>
      <td>FAILED_PRECONDITION</td>
      <td>Yes</td>
      <td>incorrect_state</td>
      <td>Could be returned if e.g., operation is rejected due to safety reasons. (E.g., vehicle moving)</td>
    </tr>
    <tr>
      <td>ABORTED</td>
      <td>Yes</td>
      <td>lost_arbitration</td>
      <td>Could e.g., be returned if service does not support concurrent requests, and there is already either a related operation ongoing or the operation is aborted due to a newer request received. Could also be used if an operation is aborted on user/driver request, e.g., physical button in vehicle pressed.</td>
    </tr>
    <tr>
      <td>OUT_OF_RANGE</td>
      <td>No</td>
      <td>invalid_argument</td>
      <td>E.g., Arguments out of range</td>
    </tr>
    <tr>
      <td>UNIMPLEMENTED</td>
      <td>No</td>
      <td>not_supported</td>
      <td>To be used if certain use-cases of the service are not implemented, e.g., if recline cannot be adjusted</td>
    </tr>
    <tr>
      <td>INTERNAL</td>
      <td>No</td>
      <td>other</td>
      <td>Internal errors, like exceptions, unexpected null pointers and similar</td>
    </tr>
    <tr>
      <td>UNAVAILABLE</td>
      <td>Yes</td>
      <td>no_service</td>
      <td>To be used if the service is temporarily unavailable, e.g., during system startup.</td>
    </tr>
    <tr>
      <td>DATA_LOSS</td>
      <td>No</td>
      <td>other</td>
      <td>No explicit use case identified on server side in SDV. Out of scope of VSC.</td>
    </tr>
    <tr>
      <td>UNAUTHENTICATED</td>
      <td>No</td>
      <td>other</td>
      <td>No explicit use case identified on server side in SDV. Out of scope of VSC.</td>
    </tr>
    <tr>
      <td>N/A</td>
      <td>N/A</td>
      <td>null</td>
      <td>VSC error not applicable if using gRPC as gRPC always return an error code</td>
    </tr>
</table>

As can be seen in the table above, there is not always a 1:1 mapping between VSC errors and gRPC errors.
Parts of this need to be sorted out in VSC context, e.g., difference between `ok` and `completed`.

### Proposed handling when implementing service defined in VSC

The starting approach when implementing a service defined in VSC in a framework using gRPC errors is to only use gRPC errors that correspond to defined VSC errors.
If a VSC method e.g., specifies only `ok` and `incorrect_state` as error codes,
then an implementation using gRPC shall, if possible, only use the gRPC error codes `OK` and `FAILED_PRECONDITION`.
If additional gRPC errors are relevant, then it shall be investigated whether or not the specified VSC list of errors can be extended.

### Comparison on actual error code usage

The current list of error codes in [VSC](https://github.com/COVESA/vehicle_service_catalog/blob/master/comfort-service.yml)
does not cover all error messages that SDV can return. Extensions are needed, but can be covered by
existing VSC error codes.
The existing VSC definitions shall preferably be extended with comments so that the meaning of the different error codes in this particular context are clarified.

<table border="1">
  <tbody>
    <tr>
      <th>VSC Method</th>
      <th>Currently defined VSC errors</th>
      <th>Used gRPC errors</th>
      <th>Proposed VSC changes</th>
    </tr>
    <tr>
      <td>move</td>
      <td>"ok", "completed", "in_progress", "interrupted"</td>
      <td>OK, OUT_OF_RANGE, INVALID_ARGUMENT, INTERNAL</td>
      <td>Add invalid_argument, other. Difference between ok and completed to be clarified. When/how is in_progress to be used? Possibly add "not_found" as used by other methods.</td>
    </tr>
    <tr>
      <td>move_component</td>
      <td>"ok", "not_found", "busy"</td>
      <td>OK, OUT_OF_RANGE, NOT_FOUND, INVALID_ARGUMENT, INTERNAL</td>
      <td>Add invalid_argument, other. </td>
    </tr>
    <tr>
      <td>current_position</td>
      <td>"ok", "not_found"</td>
      <td>OK, OUT_OF_RANGE</td>
      <td>Add invalid_argument, check VSC difference between not_found and invalid_argument </td>
    </tr>
</table>
</br>

### Other references

- [Pattern for rich error handling in gRPC](https://stackoverflow.com/questions/48748745/pattern-for-rich-error-handling-in-grpc)
- [Advanced gRPC Error Usage](https://jbrandhorst.com/post/grpc-errors/)
