# **Comprehensive Analysis of the BitChat Mesh Protocol: ESP32-C3 Integration, Network Mechanics, and Security Architecture**

## **Introduction to Decentralized Mesh Architectures and the BitChat Paradigm**

The evolution of decentralized communication infrastructure has increasingly shifted toward off-grid, peer-to-peer mobile ad-hoc networks capable of operating independently of centralized internet service providers and traditional cellular routing topologies. Announced in July 2025, the BitChat protocol represents a significant advancement within this domain.1 Originating from the foundational concepts of the early internet relay chat systems and building upon the ideological framework of prior offline messaging experiments such as Firechat, BitChat provides a highly resilient, dual-transport communication framework designed specifically for deployment in scenarios where internet connectivity is either untrustworthy, monitored, or physically unavailable.1 This includes applications in disaster recovery zones, remote rural connectivity projects, and environments characterized by severe network censorship.4

The fundamental architecture of BitChat is predicated on a dual-transport paradigm that seamlessly transitions between local, short-range radio communications and global internet-based relays.3 The primary offline transport layer utilizes Bluetooth Low Energy mesh networking, allowing mobile devices and embedded hardware to form ephemeral, ad-hoc connections that route messages through intermediate nodes without requiring user accounts, persistent digital identifiers, or centralized authentication servers.3 When peers move beyond the physical radio range of the local Bluetooth mesh, the protocol intelligently falls back to the Nostr protocol, utilizing NIP-17 "gift-wrapped" encrypted payloads and Geohash-based spatial channel identifiers to bridge localized meshes across the global internet.3

This exhaustive research report examines the technical architecture of the BitChat protocol stack, evaluating its packet serialization mechanics, cryptographic foundations via the Noise Protocol Framework, and decentralized routing logic.7 Furthermore, the analysis focuses deeply on the precise integration methodologies required to deploy the BitChat protocol on the Espressif ESP32-C3 System-on-Chip. Featuring a RISC-V microcontroller combined with embedded 2.4 GHz Wi-Fi and Bluetooth 5 hardware, the ESP32-C3 is uniquely positioned to interact with the BitChat mesh.8 By evaluating the protocol's compatibility with embedded systems, this report provides a comprehensive blueprint detailing how the ESP32-C3 can be configured as a standalone mesh relay, a passive network sniffer for protocol analysis, and a telemetry aggregation bridge for monitoring decentralized network health.8

## **The Layered Abstraction of the BitChat Protocol Stack**

To ensure that security guarantees, routing logic, and application data remain strictly decoupled from the physical transmission medium, BitChat utilizes a highly modular, four-layer protocol stack.7 This abstraction is critical for embedded systems engineering, as it allows microcontrollers like the ESP32-C3 to process network logic without being tightly bound to a specific radio transceiver implementation.7

The lowest layer of this architecture is the Transport Layer, which represents the underlying physical medium utilized for byte-level data transmission.7 While the protocol is transport-agnostic in theory, allowing for future expansions into Wi-Fi Direct or alternative frequency bands, the current implementations are heavily optimized for the Generic Attribute Profile within the Bluetooth Low Energy specification.7 The Transport Layer manages the physical radio negotiations, the establishment of connections between proximate peers, and the handling of the Maximum Transmission Unit limitations inherent to the physical spectrum.12

Situated immediately above the physical medium is the Encryption Layer, which serves as the cryptographic heart of the protocol.7 This layer is entirely responsible for establishing and managing secure communication channels using the Noise Protocol Framework.7 By isolating the cryptographic handshake, session management, and the authenticated encryption with associated data of transport messages into a dedicated layer, the protocol ensures that no plaintext routing metadata or application data ever interacts directly with the physical transport medium.7

The Session Layer operates above the cryptographic boundary, managing the structural integrity and routing metadata of the overall communication packet, internally defined as the BitchatPacket.7 The responsibilities of the Session Layer include defining message types, maintaining the Time-To-Live parameters critical for loop prevention in the mesh, executing payload fragmentation for data that exceeds the transport layer's capacity, and handling the binary serialization of the packet into a compact format.7

Finally, the Application Layer defines the structures and semantic logic of the user-facing data.7 This includes the formulation of the BitchatMessage payloads containing the actual textual chat content, the generation of delivery acknowledgments to ensure reliability, and the parsing of peer discovery announcements.7 The Application Layer dictates how the data is ultimately presented to the end user or, in the case of an embedded telemetry node, how the commands are interpreted by the hardware.7

## **Data Serialization, Framing, and Payload Optimization**

In low-bandwidth, high-latency environments characterized by frequent connection drops, the efficiency of data serialization is paramount.14 The BitChat protocol discards verbose, human-readable serialization formats such as JSON or XML in favor of a highly compact, fixed-size binary format designed explicitly to minimize bandwidth consumption and resist passive traffic analysis.7

### **Binary Packet Format and Session Layer Headers**

When a secure session has been successfully established by the Encryption Layer, peers exchange BitchatPacket structures.7 To ensure parsing efficiency on memory-constrained devices, the protocol defines a mandatory 13-byte fixed header for every packet, ensuring that the Session Layer can immediately determine the packet's routing requirements without processing the entire payload.7

| Packet Field | Size Parameter | Structural Description |
| :---- | :---- | :---- |
| **Protocol Version** | 1 byte | Indicates the current protocol specification, currently hardcoded to version 0x01.7 |
| **Message Type** | 1 byte | Categorizes the packet function, utilizing enumerations such as message, deliveryAck, noiseHandshakeInit, or fragmentStart.7 |
| **Time-To-Live (TTL)** | 1 byte | An 8-bit counter utilized exclusively for decentralized mesh routing, actively decremented at each node hop.7 |
| **Packet Timestamp** | 8 bytes | A UInt64 representation of the millisecond Unix epoch time marking the exact moment of packet instantiation.7 |
| **Optional Flags** | 1 byte | A bitmask utilized to indicate the presence of variable fields, utilizing logical flags such as hasRecipient, hasSignature, and isCompressed.7 |
| **Payload Length** | 2 bytes | A UInt16 value mathematically defining the exact byte size of the subsequent variable payload field.7 |
| **Sender Identifier** | 8 bytes | A deterministic, truncated cryptographic representation of the transmitting peer's identity.7 |
| **Recipient Identifier** | 8 bytes (Conditional) | Present exclusively if the hasRecipient flag evaluates to true. Broadcast packets utilize a hardcoded identifier of 0xFFFFFFFFFFFFFFFF.7 |
| **Encrypted Payload** | Variable size | The core semantic data of the packet, formatted according to the specifications of the Message Type field.7 |
| **Cryptographic Signature** | 64 bytes (Conditional) | An Ed25519 signature covering the packet, appended if the hasSignature flag evaluates to true.7 |

Table 1: Exhaustive byte-level mapping of the BitchatPacket binary structure managed by the Session Layer.7

### **Application Message Formatting and Sub-Payloads**

When the BitchatPacket header indicates a type of message, the internal payload field encapsulates a secondary binary-serialized structure known as the BitchatMessage.7 This structure contains the semantic data relevant to the Application Layer.7 To manage variable-length textual data, the serialization mechanism relies on fixed-size length prefixes immediately preceding each variable field.7

| Application Field | Size Parameter | Structural Description |
| :---- | :---- | :---- |
| **Application Flags** | 1 byte | A secondary bitmask specific to the application state, defining boolean values for isRelay, isPrivate, and hasOriginalSender.7 |
| **Message Timestamp** | 8 bytes | A UInt64 millisecond timestamp denoting the exact creation time of the text content.7 |
| **Message UUID** | 1 byte length prefix \+ Variable | A Universally Unique Identifier string used for message tracking and deduplication in the user interface.7 |
| **Sender Nickname** | 1 byte length prefix \+ Variable | The UTF-8 encoded string representing the human-readable nickname of the transmitting user.7 |
| **Chat Content** | 2 bytes length prefix \+ Variable | The primary UTF-8 encoded textual payload generated by the user.7 |
| **Original Sender** | 1 byte length prefix \+ Variable | An optional field containing the nickname of the original author, utilized exclusively if the message is a multi-hop relay broadcast.7 |
| **Recipient Nickname** | 1 byte length prefix \+ Variable | An optional field containing the targeted nickname for direct, one-to-one private communications.7 |

Table 2: Serialization schema of the inner BitchatMessage payload managed by the Application Layer.7

The rigid implementation of these binary protocols has created implementation challenges across different hardware and software environments. Engineering audits have revealed that the iOS and Android versions of the protocol have historically suffered from serialization mismatches due to underlying architectural differences in byte ordering.16 Swift and Kotlin ecosystems often default to different endianness for multi-byte integers, and variances in struct padding and field alignment have resulted in silent parsing failures where packets broadcast by an Android device fail to decode properly on iOS hardware.16 For an embedded systems engineer developing C or C++ firmware for the ESP32-C3, strict adherence to network byte order and careful management of compiler struct packing directives are absolutely critical to maintaining binary protocol compatibility with the mobile applications.16

### **Traffic Analysis Resistance via Cryptographic Padding**

A fundamental privacy vulnerability in many modern messaging protocols is the susceptibility to passive traffic analysis. Even when packet contents are entirely encrypted, adversaries monitoring the radio frequency spectrum can correlate the exact byte size of a transmission with specific user behaviors.7 For example, a uniquely small packet might consistently indicate a typing notification, while a massive packet might indicate an image transfer.17

To neutralize this attack vector, the BitChat protocol integrates a robust cryptographic padding mechanism.7 Prior to encryption, the Session Layer enforces a PKCS\#7-style padding scheme, inflating the total size of the BitchatPacket to the nearest standard block size.7 The protocol utilizes specific block thresholds of 256, 512, 1024, or 2048 bytes.7 Consequently, an environmental sniffer monitoring the Bluetooth spectrum will observe a highly uniform distribution of packet sizes.17 A peer transmitting a single character acknowledgment and a peer transmitting a complex cryptographic key exchange will both appear to transmit identical 512-byte blocks, effectively blinding passive surveillance mechanisms.7

## **Decentralized Routing Logic and the Store-and-Forward Mesh**

The architectural defining feature of the BitChat protocol is its ability to route data across physical distances that far exceed the radio range of any single transmission unit.1 By transforming every participating mobile phone and embedded microcontroller into a simultaneous client and relay server, the network establishes a self-healing, ad-hoc topology.1 The protocol utilizes a sophisticated store-and-forward flooding mechanism, augmented by probabilistic data structures to manage the inherent chaos of an unstructured mesh.7

### **Node Relaying and the Time-To-Live Mechanism**

When a node receives a packet via the Bluetooth interface, the Session Layer examines the addressing metadata. If the packet is a broadcast or if the recipient identifier does not match the node's local identity, the node is expected to act as an intermediate relay.7 To prevent isolated packets from bouncing between peers indefinitely and causing network congestion, the protocol enforces strict lifecycle management through the Time-To-Live field located in the BitchatPacket header.7

The peer originating the transmission populates the initial 8-bit TTL field.7 As the packet propagates through the physical environment, every node that receives and subsequently re-broadcasts the packet is mathematically obligated to decrement the TTL value by exactly one.7 If an intermediate node decrements the value and the TTL reaches zero, the node is explicitly forbidden from relaying the packet any further.7 The protocol enforces a hardcoded maximum threshold of seven hops, ensuring that the mesh remains geographically localized and protecting the limited bandwidth of the Bluetooth spectrum from being overwhelmed by distant chatter.3

### **Broadcast Storm Mitigation via Optimized Bloom Filters**

In a densely populated mesh environment, such as a protest or a music festival, naive flooding routing algorithms inevitably trigger broadcast storms, where hundreds of devices repeatedly transmit the same packet to each other, resulting in exponential message duplication and total network failure.10 To suppress redundant transmissions while minimizing memory overhead on embedded devices, the BitChat protocol integrates an OptimizedBloomFilter to track recently processed packet identifiers.7

The Bloom filter represents an elegant engineering compromise for constrained systems.7 Rather than storing the full cryptographic hash of every observed packet in a massive, memory-intensive database, the protocol utilizes a compact bit array of size ![][image1].7 When an ESP32-C3 node receives a packet, it processes the packet's unique identifier through ![][image2] distinct hash functions.7 These hash functions output ![][image2] array indices. The firmware then queries the bit array at those specific indices.7

The routing logic proceeds according to strict conditional pathways:

* **Packet Rejection:** If the bits at all ![][image2] indices are already set to 1, the protocol probabilistically assumes that the packet has been seen before. The node immediately discards the packet, halting its propagation along that specific physical path.7  
* **Packet Acceptance and Forwarding:** If any of the queried bits evaluate to 0, the node concludes with absolute mathematical certainty that the packet is novel. The node updates the Bloom filter by setting the respective bits to 1, executes the TTL decrement sequence, and re-broadcasts the payload to all currently connected peer devices, explicitly excluding the specific node from which the packet was just received.7

The probabilistic nature of the Bloom filter implies an inherent risk of false positives, a scenario where the overlapping bits of previously processed packets trick the algorithm into incorrectly classifying a genuinely new packet as a duplicate.7 However, the fundamental mathematics of the structure guarantee that there are zero false negatives.7 The BitChat protocol embraces this design constraint, operating under the assumption that the inherent redundancy of the gossip topology will compensate for minor routing failures.7 If an ESP32-C3 incorrectly drops a packet due to a Bloom filter collision, the high density of the surrounding mesh ensures that the intended recipient will likely receive the identical packet from a different neighboring node along an alternative physical path.7

### **MTU Limitations and the Fragmentation Subsystem**

The physical realities of Bluetooth Low Energy dictate strict limitations on the volume of data that can be transmitted in a single radio burst. While modern Bluetooth 5.0 specifications allow the negotiation of the Maximum Transmission Unit up to 517 bytes, many legacy devices and constrained environments default to significantly lower thresholds.12 Because the BitChat cryptographic padding mechanism can inflate packet sizes up to 2048 bytes, the Session Layer must implement an independent fragmentation protocol.7

When a payload exceeds the negotiated MTU of a specific peer-to-peer connection, the transmitting node splices the padded ciphertext into sequential frames.7 The protocol defines three highly specialized control packets to govern this sequence:

1. **fragmentStart**: A metadata packet broadcast prior to transmission, defining the total aggregate size of the impending message and the exact number of sequential fragments the receiving node should expect.7  
2. **fragmentContinue**: A series of carrier packets that stream the intermediate binary chunks of the payload across the physical gap.7  
3. **fragmentEnd**: The terminating packet that delivers the final data chunk and signals the receiving node's memory buffer to lock the sequence and initiate cryptographic reassembly.7

For an embedded systems engineer attempting to deploy BitChat on the ESP32-C3, managing this fragmentation subsystem represents a significant memory allocation challenge.23 The firmware must allocate concurrent holding buffers for multiple incoming streams, implementing aggressive garbage collection algorithms to discard incomplete or abandoned fragment sequences, thereby preventing targeted memory exhaustion attacks from malicious nodes.23

## **Cryptographic Primitives and the Noise Protocol Architecture**

The fundamental security model of the BitChat protocol completely eschews proprietary or bespoke cryptographic algorithms.17 Instead, the Encryption Layer is entirely anchored by the Noise Protocol Framework, a highly regarded, peer-reviewed cryptographic framework utilized by industry-standard systems such as WireGuard and WhatsApp.7 The framework defines a series of state machines and handshake patterns that systematically combine cryptographic primitives into verifiably secure channels.7

### **The Noise\_XX\_25519\_ChaChaPoly\_SHA256 Implementation**

The specific protocol variant implemented by BitChat is Noise\_XX\_25519\_ChaChaPoly\_SHA256.7 The selection of the XX handshake pattern is critical for the operational realities of an ad-hoc mesh network, as it supports mutual authentication and the secure transmission of static public keys between peers who have no prior knowledge of each other.7

The cryptographic suite selected for this implementation relies on the following advanced mathematical primitives:

* **Diffie-Hellman Key Exchange (Curve25519):** A high-performance elliptic curve algorithm used to establish a shared mathematical secret between peers.7 Upon initialization, an ESP32-C3 node generates a cryptographically random 32-byte private key and derives the corresponding public key using the X25519 mathematical function.19 This provides the foundation for the ephemeral and static keys required by the Noise framework.19  
* **Identity Binding and Authentication (Ed25519):** Operating alongside the Curve25519 key exchange, the protocol utilizes the Ed25519 signature scheme to bind user identities to their network actions.7 The node utilizes a dedicated Ed25519 signing key pair to generate unforgeable digital signatures over protocol announcements, ensuring non-repudiation and mitigating rudimentary impersonation attacks.7  
* **Authenticated Encryption (ChaCha20-Poly1305):** A highly efficient, stream-based Authenticated Encryption with Associated Data cipher suite.7 Once the three-part Noise handshake is successfully completed, the resulting shared secret is passed through a key derivation function to generate symmetric ChaCha20 encryption keys.19 The Poly1305 Message Authentication Code ensures that any tampering with the ciphertext by a malicious intermediate relay node is immediately detected, leading to the packet being dropped.26  
* **Cryptographic Hashing (SHA-256):** The standard hash function utilized throughout the protocol stack.26 It is used to maintain the continuous running hash of the Noise handshake state, ensuring that neither party can manipulate the negotiation parameters.29 Furthermore, the protocol generates unique user fingerprints by applying the SHA-256 algorithm to the user's Noise static public key, allowing for out-of-band identity verification.7

### **The Cryptographic Handshake and State Maintenance**

During the establishment of a secure channel over the Bluetooth mesh, the two interacting nodes execute the three-phase XX handshake sequence.7 The core of this mechanism relies on a meticulously maintained set of state variables on both the transmitting and receiving devices 29:

* ![][image3] and ![][image4]: Variables representing the local node's long-term static identity key pair and its newly generated, session-specific ephemeral key pair.29  
* ![][image5] and ![][image6]: Variables storing the remote node's static and ephemeral public keys as they are received over the radio interface.29  
* ![][image7]: The handshake hash, a continuous accumulator that securely hashes all preceding protocol tokens, public keys, and cryptographic payloads exchanged over the connection.29  
* ![][image8]: The chaining key, which acts as a secure cryptographic sponge. As Diffie-Hellman calculations are completed between the various combinations of ephemeral and static keys, the outputs are iteratively absorbed by the chaining key.29

Upon the successful conclusion of the third handshake message, the chaining key mathematically derives two distinct, unidirectional encryption keys to be utilized by the ChaCha20-Poly1305 cipher.26 This complex state management ensures total forward secrecy; even if an adversary compromises an ESP32-C3 node and extracts its long-term static Curve25519 private key from memory, they cannot retroactively decrypt intercepted historical traffic because the ephemeral session keys have been securely destroyed.3

### **The Encapsulation Paradox in Private Relaying**

A rigorous analysis of the BitChat technical specifications reveals a complex architectural paradox regarding the routing of private, end-to-end encrypted messages across the mesh topology.7

The protocol explicitly mandates that for private communications directed to a specific recipientID, intermediate relay nodes must forward the entire, encrypted Noise message as a completely opaque black box.7 Because the intermediate ESP32-C3 nodes do not possess the necessary session keys negotiated during the original sender and receiver's XX handshake, they cannot decrypt the outer transport envelope to access the inner BitchatPacket or its textual payload.7

This creates a fundamental contradiction in the routing logic: if the Time-To-Live field and the unique packet identifier required by the OptimizedBloomFilter are cryptographically sealed within the BitchatPacket structure as defined by the Session Layer serialization, the intermediate nodes cannot technically access them.7 An intermediate relay node cannot decrement a TTL field that it cannot decrypt.7

Consequently, to maintain functional mesh flooding, the physical transport implementation must inherently rely on a secondary, unencrypted outer header system.30 In practice, this likely involves exposing the hop count and a pseudo-identifier directly within the raw Bluetooth Low Energy advertisement metadata or the outer Generic Attribute Profile characteristics.7 For firmware engineers, this implies that an ESP32-C3 operating as a bridge must carefully manipulate these unencrypted physical layer variables to execute the routing logic without ever attempting to tamper with the immutable inner Noise ciphertext.7

## **Hardware Suitability and Performance: The Espressif ESP32-C3 Profile**

Deploying the comprehensive BitChat protocol stack onto an embedded microcontroller requires a careful evaluation of the hardware's architectural capabilities.31 The Espressif ESP32-C3 is a highly compelling platform for interfacing with the mesh, but it operates under strict resource constraints that profoundly influence firmware design.8

### **RISC-V Architectural Constraints and Memory Mapping**

Unlike the dual-core Tensilica architecture of the original ESP32, the ESP32-C3 is built around a single-core, 32-bit RISC-V processor clocked at a maximum of 160 MHz.23 The silicon is equipped with a modest 400 KB of internal SRAM and 384 KB of ROM.23

Executing a complex decentralized messaging protocol on this architecture requires extreme optimization of the memory map. The underlying FreeRTOS operating system, the Wi-Fi stack, and the Bluetooth Low Energy controller consume a significant baseline of the available SRAM.9 The remaining dynamic heap memory must be meticulously partitioned to support the BitChat application logic.23 This includes maintaining the static arrays for the OptimizedBloomFilter, allocating the connection state variables for multiple concurrent Noise protocol handshakes, and dedicating contiguous memory buffers to handle the reassembly of fragmented 2048-byte application payloads.7 Failure to implement rigorous garbage collection will inevitably lead to heap fragmentation and fatal system crashes during periods of high network congestion.10

### **Cryptographic Hardware Acceleration and Latency Benchmarks**

The execution of the Noise XX pattern involves computationally intense elliptic curve mathematics. If executed entirely via software libraries on a 160 MHz microcontroller, the resulting latency can easily exceed the strict timeout parameters of the underlying Bluetooth connection, leading to persistent handshake failures.10

To mitigate this bottleneck, the ESP32-C3 silicon incorporates dedicated hardware acceleration blocks for specific cryptographic primitives.34 Exploiting these hardware peripherals is mandatory for achieving functional routing performance.

| Cryptographic Algorithm | Execution Methodology | Benchmark Performance Estimate |
| :---- | :---- | :---- |
| **SHA-256** | Software (Standard mbedTLS) | \~1.62 MB/s 35 |
| **SHA-256** | Hardware Accelerated Peripheral | \~21.73 MB/s 35 |
| **AES-256-CCM** | Software Emulation | \~0.54 MB/s 35 |
| **Curve25519 (Key Exchange)** | Software (ESP32-S2 baseline) | Variable latency ranging from \~7 ms to 100+ ms depending on optimization.33 |

Table 3: Estimated cryptographic throughput and latency benchmarks relevant to the execution of the Noise Protocol Framework on the ESP32-C3 architecture.33

While the hardware acceleration dramatically improves the throughput of hashing operations and symmetric encryption, the complex elliptic curve operations (Curve25519 and Ed25519) often lack dedicated silicon support on the base C3 variants.28 Consequently, firmware engineers utilizing frameworks such as the ESP-IDF must rely on highly optimized third-party cryptographic libraries.23 Implementations utilizing wolfSSL or stripped-down, bare-metal compilations of libsodium have demonstrated up to ten times the performance of generic software algorithms, ensuring that the critical key exchange calculations complete within acceptable operational tolerances.23

## **Network Interaction: Participating and Relaying on the ESP32-C3**

For an ESP32-C3 to seamlessly integrate into the decentralized mesh, the firmware must flawlessly implement the specific Bluetooth Low Energy configurations demanded by the mobile applications.15 The protocol dictates that every node must operate concurrently as a GATT Server, broadcasting its availability, and a GATT Client, actively scanning the environment for new peers.15

### **Service and Characteristic UUID Integration**

The Bluetooth specification utilizes 128-bit Universally Unique Identifiers to organize data hierarchies.37 To identify legitimate peers and establish communication pathways, the ESP32-C3 must precisely configure its radio advertisements using the custom UUIDs defined by the BitChat project.15

The primary interface exposed to the mesh is designated as the BluetoothMeshService.15

* **Primary Service UUID:** The firmware must advertise and bind to the specific 128-bit identifier f47b5e2d-4a9e-4c5a-9b3f-8e1d2c3a4b5c.12  
* **Data Characteristic UUID:** All read, write, and notification interactions are channeled through a single, specialized characteristic identified by the UUID a1b2c3d4-e5f6-4a5b-8c9d-0e1f2a3b4c5d.15

During the initial discovery phase, the ESP32-C3 embeds its ephemeral cryptographic PeerID and its localized user nickname directly into the advertisement data associated with the Service UUID.15 As neighboring smartphones or relay nodes scan the 2.4 GHz spectrum, they filter the background noise for this specific identifier.40 When a match is detected, the nodes initiate a connection request, negotiate the highest possible Maximum Transmission Unit, and subsequently trigger the Noise cryptographic handshake by transmitting Handle Value Notifications over the Characteristic UUID.12

## **Network Sniffing and Passive Intelligence Gathering**

Because the Bluetooth Low Energy transport layer inherently broadcasts radio frequency energy omnidirectionally into the surrounding physical environment, the BitChat mesh is inherently exposed to sophisticated physical-layer surveillance.42 The ESP32-C3 is uniquely equipped to act as an undetectable, low-cost protocol analyzer and intelligence-gathering node.43

### **Passive Protocol Analysis and MAC Randomization Evasion**

By configuring the Bluetooth controller via the ESP-IDF APIs, the ESP32-C3 can be placed into a passive scanning mode.44 In this operational state, the microcontroller instructs the radio transceiver to silently capture all BLE advertisement packets propagating through the air, without ever attempting to initiate a connection or transmit a response.43 Consequently, the presence of the sniffer node remains entirely unknown to the participants of the mesh network.42

Modern mobile operating systems implement robust privacy mechanisms, specifically the continuous randomization of hardware MAC addresses, to thwart physical tracking.45 If a sniffer attempts to track a device using its physical MAC address, the identifier will shift unpredictably every few minutes.45 However, the BitChat protocol inadvertently circumvents this protection through its application-layer discovery mechanism.15

Because peers must find one another to establish the mesh, they continuously broadcast their ephemeral PeerID and their plaintext user nicknames within the advertisement payload of the f47b5e2d-4a9e-4c5a-9b3f-8e1d2c3a4b5c Service UUID.15 An ESP32-C3 configured with a strict capture filter targeting this specific UUID can entirely ignore the rotating MAC addresses, instead logging the unencrypted application identifiers.15 This allows the sniffer to accurately plot the physical density, proximity, and persistent presence of specific users within a given geographic area over extended periods.15

### **Deep Packet Interception and Traffic Correlation**

For deeper environmental intelligence, the dual-radio capabilities of the ESP32-C3 can be fully utilized.8 While the Bluetooth transceiver passively logs the unencrypted GATT advertisements and monitors the volume of padded, encrypted traffic traversing the connection channels, the secondary radio can be engaged in Wi-Fi Sniffer Mode.8 By capturing raw 802.11 management frames, probe requests, and associated signal strengths concurrently with the BLE traffic, an analyst can correlate the temporal patterns of the BitChat mesh with the broader Wi-Fi signatures of the surrounding crowd.8

The raw hexadecimal payloads captured by the ESP32-C3 can be streamed in real-time over an out-of-band link to advanced protocol analysis software, such as Wireshark or Ellisys suites.15 While the ultimate textual contents remain protected by the Noise XX forward secrecy, the sniffer can conduct sophisticated traffic analysis.7 By analyzing the transmission intervals, tracking the unencrypted fragmentation headers (fragmentStart, fragmentEnd), and observing the hop-by-hop relay propagation, analysts can deduce the topological structure of the mesh, identifying the critical bottleneck nodes acting as central communication hubs for the localized network.7

## **Telemetry Aggregation and the Metrics Dashboard**

While the BitChat protocol emphasizes user privacy, the operational realities of deploying and maintaining a functional decentralized network require robust diagnostic oversight.10 To ensure that a massive deployment of nodes does not collapse under the weight of a broadcast storm, network operators require actionable intelligence regarding the health of the mesh. The ESP32-C3 provides the perfect platform for building a privacy-preserving telemetry bridge.10

### **Bridging the Offline Mesh to Cloud Observability**

A stationary ESP32-C3 node can be deployed to participate simultaneously in the offline Bluetooth mesh and the online internet infrastructure.10 The firmware architecture involves the microcontroller executing the standard BluetoothMeshService stack to accept incoming P2P connections and relay routing traffic, exactly as a mobile client would.10 However, a secondary FreeRTOS background task continuously monitors the internal state variables of the protocol stack.10

To respect the core tenets of the system, this telemetry node never attempts to log, store, or transmit the cryptographic payloads of the communications.10 Instead, it extracts purely statistical network metadata.10 This metadata is serialized into a highly optimized, lightweight JSON payload and transmitted out-of-band over the 2.4 GHz Wi-Fi interface, utilizing the MQTT protocol to push the data to a centralized observability stack, such as Prometheus or a Grafana dashboard.10

### **Key Performance Indicators for Mesh Topology**

An effectively designed dashboard fed by multiple geographically distributed ESP32-C3 telemetry nodes can track critical Key Performance Indicators detailing the physical stability of the network:

* **Radio Frequency Degradation:** By continuously monitoring the Received Signal Strength Indicator (RSSI) of incoming connections, operators can map the physical deterioration of the mesh architecture, identifying geographic dead zones or areas requiring additional relay hardware.44  
* **Routing Overhead and Efficiency:** Extracting the distribution of incoming Time-To-Live parameters provides insight into the average hop count required for a packet to traverse the local environment.7  
* **Broadcast Storm Identification:** By logging the precise number of packets violently rejected by the OptimizedBloomFilter, the dashboard can measure the redundancy overhead of the flooding algorithm, providing early warning signs of connection looping or targeted network flooding attacks.7  
* **Cryptographic Hardware Latency:** Benchmarking the average milliseconds required to complete the Noise XX handshake provides critical visibility into the processing bottlenecks of the hardware, exposing instances where nodes are suffering from denial-of-service attempts via connection flooding.10

## **Architectural Vulnerabilities and Exploitation Vectors**

Despite the rigorous implementation of the Noise Protocol Framework, comprehensive security audits, static code analysis, and community vulnerability disclosures have revealed multiple critical architectural flaws within the application and session layers of the BitChat protocol.15 These vulnerabilities represent significant exploitation vectors for malicious actors operating within Bluetooth radio range.15

### **Integer Truncation and Firmware Buffer Overflows**

A high-severity vulnerability exists within the protocol's binary serialization logic, specifically concerning the encoding of the 16-bit Payload Length field (VULN-02, CVSS v3.1 Score: 7.5).24 The core software lacks adequate input validation when parsing massive incoming payloads.24

If a sophisticated adversary crafts a deliberately malformed BitchatPacket where the payloadDataSize wildly exceeds the mathematical maximum of 65,535 bytes (the absolute limit of a UInt16 integer), the serialization algorithm silently truncates the value.24 For instance, a payload engineered to contain exactly 70,000 bytes will wrap around the integer limit, resulting in a corrupted packet header falsely declaring a length of 4,464 bytes.24

When an embedded device, such as the ESP32-C3, receives this transmission, the firmware reads the truncated header and allocates a disproportionately small memory buffer in the SRAM.24 As the physical radio continues to stream the remaining payload bytes, the incoming data catastrophically overwrites adjacent memory addresses, resulting in a severe buffer overflow.24 In an RTOS environment, this immediately triggers a fatal exception, plunging the node into a persistent reboot loop and executing a devastating Denial-of-Service attack across the localized mesh.24

### **Cryptographic Signature Tampering and Command Bypass**

While the Noise layer guarantees the integrity of the encrypted transport, severe flaws exist in the application-layer validation logic.15 Security researchers have identified that the cryptographic Signature field situated within the BitchatPacket structure is treated as entirely optional, governed loosely by the hasSignature boolean flag.15 More critically, when the signature is enforced, it mathematically covers only the inner payload, leaving the unencrypted header fields entirely vulnerable to active tampering by intermediate nodes.15

This weak message integrity design facilitates devastating logic bypass attacks.15 The protocol implements IRC-style administrative commands for managing public channel infrastructure.3 However, highly privileged commands, such as /transfer used for delegating channel ownership, are validated locally by the initiator's application but lack any distributed cryptographic verification among the receiving peers.15 Consequently, a malicious node can forge a /transfer packet without possessing the legitimate owner's private keys, successfully hijacking ownership of public channels and stripping their encryption parameters without authorization.15

### **The Sybil Threat: Identity Without Cryptographic Proof**

The most fundamental architectural flaw documented within the BitChat network revolves around the concept of "Identity Without Proof" (classified as a Zero-Day vulnerability under CWE-345/CWE-347).48 The security model of any decentralized network relies heavily on the ability to trust the identity of participating peers.48

BitChat fails to secure its initial discovery phase. When a node transmits an announce packet to broadcast its presence and bind an ephemeral identity to a human-readable nickname, the protocol lacks a mandatory cryptographic verification mechanism to prove ownership of the asserted identity.15 This oversight allows threat actors to indiscriminately spoof nicknames and flood the Bluetooth spectrum with forged presence announcements.48 By injecting these spoofed identities into the mesh before legitimate peers can initiate a secure Noise handshake, an attacker can trivially execute sophisticated Man-in-the-Middle interceptions and Sybil attacks, comprehensively undermining the trust architecture of the entire local deployment.15

## **Existing Ecosystem Implementations and ESP32 Firmware Projects**

The open-source community has aggressively expanded the reach of the BitChat protocol beyond traditional mobile operating systems, exploring highly innovative hardware integrations utilizing Espressif microcontrollers to dramatically extend the capabilities of the decentralized network.9

### **Long-Range Relaying: The Meshtastic LoRa Integration**

A critical physical limitation of the standard BitChat protocol is the extremely short operational range of the Bluetooth spectrum, typically restricted to 30 meters under optimal conditions.18 To overcome this constraint, engineers have successfully proposed and architected integration pathways with the Meshtastic project, a decentralized mesh network operating over Long Range (LoRa) radio frequencies.14

In this hybrid topology, an ESP32 microcontroller paired with a LoRa transceiver acts as an environmental gateway.14 A smartphone running the BitChat application establishes a standard Bluetooth Low Energy connection to the ESP32.14 The microcontroller firmware accepts the payload, extracts the fully encrypted BitchatPacket, and encapsulates it within a secondary Meshtastic LoRa frame.14 The ESP32 then broadcasts the signal over the LoRa spectrum, achieving physical transmission distances of up to 10 kilometers in rural environments.14

A corresponding receiving gateway miles away captures the LoRa packet, strips the outer encapsulation, and injects the preserved BitchatPacket back into its localized Bluetooth mesh.14 Because the packet is continuously protected by the underlying Noise encryption, the intermediate LoRa network functions as a mathematically secure, transparent transport tunnel.14 However, operators implementing this bridge must strictly account for the extreme bandwidth constraints of the LoRa protocol, which operates at speeds between 300 bps and 2.4 kbps, rendering large textual messages or fragmented payloads highly susceptible to transmission failures.14

### **Dynamic Protocol Switching with MeshCore**

The MeshCore-BitChat project demonstrates the complexities of operating multi-protocol environments on constrained hardware.12 Utilizing advanced boards such as the ESP32-S3 and the Seeed Xiao series, the specialized firmware allows a single device to dynamically bridge the proprietary MeshCore communication networks with the standardized BitChat application.12

Due to the rigid memory limitations governing the size of Bluetooth advertisement payloads, the ESP32 cannot simultaneously advertise the required Service UUIDs for both protocols.50 To resolve this physical constraint, the firmware implements dynamic, menu-based or button-based mode switching.50 When an operator physically interacts with the hardware interface, the firmware invokes the internal setBitChatMode() function, gracefully tearing down the active connections and aggressively swapping the advertised radio data to the required BitChat UUID of f47b5e2d-4a9e-4c5a-9b3f-8e1d2c3a4b5c.50 This approach requires meticulous state management within the ESP32 to prevent memory leaks during rapid protocol transitions.50

### **Zephyr RTOS and Native C Implementations**

The push to eliminate reliance on tethered smartphones has resulted in dedicated firmware projects designed to run the protocol natively on the silicon. The bitchat-esp32 project utilizes the Zephyr Real-Time Operating System to deploy the application stack directly onto an ESP32-C6.9 This standalone implementation grants the microcontroller full autonomy, allowing it to execute the complex cryptographic handshakes, decode the incoming ciphertext, and print the resulting messages directly to a physical UART terminal without any mobile device involvement.9

Similarly, projects such as ckb-light-esp are spearheading the development of highly optimized, from-scratch C implementations of the Noise X25519 protocol logic specifically tailored for the memory map of the ESP32 architecture.49 By bypassing bulky mobile operating systems and moving the cryptographic processing directly onto embedded C environments, these projects lay the foundational framework for building massive, invisible arrays of autonomous routing nodes capable of sustaining the BitChat network indefinitely.49

## **Conclusions and Embedded Engineering Outlook**

The BitChat protocol represents a highly ambitious and architecturally sophisticated approach to decentralized, off-grid communication.3 By forcefully decoupling the physical transport mechanisms from the cryptographic state machine, the protocol ensures that the rigorous security guarantees provided by the Noise Protocol Framework remain intact regardless of whether the data is traversing a localized Bluetooth mesh, bridging over a long-range LoRa link, or being routed through global internet relays via Nostr.3 Its adoption of fixed-size binary serialization and cryptographic padding demonstrates a nuanced understanding of traffic analysis mitigation, while the implementation of store-and-forward flooding augmented by probabilistic Bloom filters highlights a pragmatic approach to the chaos of ad-hoc network topologies.7

However, the protocol is not without significant engineering challenges. Severe vulnerabilities in its application-layer input validation, specifically regarding integer truncation and unverified identity announcements, expose the network to catastrophic denial-of-service and impersonation attacks.24 Furthermore, the routing mechanics rely on an encapsulation paradox, demanding that intermediate relay nodes process traffic using unencrypted outer metadata headers while the critical inner payloads remain cryptographically sealed.7

For the embedded systems engineer, the Espressif ESP32-C3 provides a highly versatile, low-cost platform to interact deeply with this ecosystem.8 While its RISC-V architecture and limited SRAM necessitate extreme optimization of memory buffers, particularly when handling payload fragmentation, its hardware acceleration peripherals for hashing and symmetric encryption provide the requisite throughput to sustain the complex Noise handshake.23 Whether configured as a passive protocol analyzer sniffing advertisement UUIDs, a telemetry bridge shipping localized mesh health metrics to centralized MQTT dashboards, or a physical layer gateway extending the network's reach via LoRa transceivers, the ESP32-C3 stands as a critical building block in the continued evolution of decentralized, resilient communication infrastructure.10

#### **Works cited**

1. Early Review of the BitChat Protocol, accessed April 7, 2026, [http://www.conf-icnc.org/2026/papers/p339-biagioni.pdf](http://www.conf-icnc.org/2026/papers/p339-biagioni.pdf)  
2. BitChat \- Wikipedia, accessed April 7, 2026, [https://en.wikipedia.org/wiki/BitChat](https://en.wikipedia.org/wiki/BitChat)  
3. permissionlesstech/bitchat: bluetooth mesh chat, IRC vibes \- GitHub, accessed April 7, 2026, [https://github.com/permissionlesstech/bitchat](https://github.com/permissionlesstech/bitchat)  
4. Bit-chats: A Framework for Secure and Efficient Decentralized Communication over Bluetooth Mobile Ad-Hoc Networks \- ScienceOpen, accessed April 7, 2026, [https://www.scienceopen.com/hosted-document?doi=10.14293/PR2199.002312.v1](https://www.scienceopen.com/hosted-document?doi=10.14293/PR2199.002312.v1)  
5. Bitchat Tops Jamaica App Charts as Hurricane Melissa Cuts Connectivity \- Decrypt, accessed April 7, 2026, [https://decrypt.co/346748/bitchat-tops-jamaica-app-charts-hurricane-melissa-cuts-connectivity](https://decrypt.co/346748/bitchat-tops-jamaica-app-charts-hurricane-melissa-cuts-connectivity)  
6. @bitchat-sdk/nostr 0.1.0 on npm \- Libraries.io \- security & maintenance data for open source software, accessed April 7, 2026, [https://libraries.io/npm/@bitchat-sdk%2Fnostr](https://libraries.io/npm/@bitchat-sdk%2Fnostr)  
7. bitchat/WHITEPAPER.md at main · permissionlesstech/bitchat · GitHub, accessed April 7, 2026, [https://github.com/permissionlesstech/bitchat/blob/main/WHITEPAPER.md](https://github.com/permissionlesstech/bitchat/blob/main/WHITEPAPER.md)  
8. Wi-Fi Modes \- ESP32-C3 \- — ESP-IDF Programming Guide v6.0 documentation, accessed April 7, 2026, [https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/api-guides/wifi-driver/wifi-modes.html](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/api-guides/wifi-driver/wifi-modes.html)  
9. hackerhouse-opensource/bitchat-esp32 \- GitHub, accessed April 7, 2026, [https://github.com/hackerhouse-opensource/bitchat-esp32](https://github.com/hackerhouse-opensource/bitchat-esp32)  
10. Anyone did work on ESP32 or Arduino RP2040 port of Bitchat? \- Reddit, accessed April 7, 2026, [https://www.reddit.com/r/bitchat/comments/1mlrde8/anyone\_did\_work\_on\_esp32\_or\_arduino\_rp2040\_port/](https://www.reddit.com/r/bitchat/comments/1mlrde8/anyone_did_work_on_esp32_or_arduino_rp2040_port/)  
11. Feedback: Location Share on Mesh Network · Issue \#4 · permissionlesstech/bitchat \- GitHub, accessed April 7, 2026, [https://github.com/jackjackbits/bitchat/issues/4](https://github.com/jackjackbits/bitchat/issues/4)  
12. MeshCore-BitChat/docs/bitchat-bridge.md at feature/bitchat-bridge · jooray/MeshCore-BitChat \- GitHub, accessed April 7, 2026, [https://github.com/jooray/MeshCore-BitChat/blob/feature/bitchat-bridge/docs/bitchat-bridge.md](https://github.com/jooray/MeshCore-BitChat/blob/feature/bitchat-bridge/docs/bitchat-bridge.md)  
13. How Bluetooth works. From peripherals to peer-to-peer | by pam \- Medium, accessed April 7, 2026, [https://christinepamela.medium.com/how-bluetooth-works-e4b0a60f034d](https://christinepamela.medium.com/how-bluetooth-works-e4b0a60f034d)  
14. BitChat over LoRa: a decentralized, offline messaging framework using ESP32 and meshtastic · Issue \#180 \- GitHub, accessed April 7, 2026, [https://github.com/permissionlesstech/bitchat/issues/180](https://github.com/permissionlesstech/bitchat/issues/180)  
15. More Bitchat Challenges: Message Tampering & Command Bypass | by pogen300 | Medium, accessed April 7, 2026, [https://medium.com/@slugisawesome/more-bitchat-challenges-message-tampering-command-bypass-e6940ddc4be4](https://medium.com/@slugisawesome/more-bitchat-challenges-message-tampering-command-bypass-e6940ddc4be4)  
16. \[Cross-Platform Bug\] Android → iOS communication fails due to packet serialization mismatch · Issue \#939 · permissionlesstech/bitchat \- GitHub, accessed April 7, 2026, [https://github.com/permissionlesstech/bitchat/issues/939](https://github.com/permissionlesstech/bitchat/issues/939)  
17. Web3 Messaging Revolution: Jack Dorsey's Bitchat Blockchain Alternative to WhatsApp, accessed April 7, 2026, [https://yellow.com/learn/web3-messaging-revolution-jack-dorseys-bitchat-blockchain-alternative-to-whatsapp](https://yellow.com/learn/web3-messaging-revolution-jack-dorseys-bitchat-blockchain-alternative-to-whatsapp)  
18. (PDF) BitChat: A Decentralized Blockchain-Integrated Messaging Platform \- ResearchGate, accessed April 7, 2026, [https://www.researchgate.net/publication/393777552\_BitChat\_A\_Decentralized\_Blockchain-Integrated\_Messaging\_Platform](https://www.researchgate.net/publication/393777552_BitChat_A_Decentralized_Blockchain-Integrated_Messaging_Platform)  
19. Bitchat: Decentralized, P2P Communication using Bluetooth \- Soln.Tech, accessed April 7, 2026, [https://soln.tech/blog/bitchat](https://soln.tech/blog/bitchat)  
20. permissionlesstech/bitchat-android: bluetooth mesh chat, IRC vibes \- GitHub, accessed April 7, 2026, [https://github.com/permissionlesstech/bitchat-android](https://github.com/permissionlesstech/bitchat-android)  
21. (PDF) Article title: Bit-chats: A Framework for Secure and Efficient Decentralized Communication over Bluetooth Mobile Ad-Hoc Networks \- ResearchGate, accessed April 7, 2026, [https://www.researchgate.net/publication/397804478\_Article\_title\_Bit-chats\_A\_Framework\_for\_Secure\_and\_Efficient\_Decentralized\_Communication\_over\_Bluetooth\_Mobile\_Ad-Hoc\_Networks](https://www.researchgate.net/publication/397804478_Article_title_Bit-chats_A_Framework_for_Secure_and_Efficient_Decentralized_Communication_over_Bluetooth_Mobile_Ad-Hoc_Networks)  
22. Part G Generic Attribute Profile (GATT) \- Bluetooth, accessed April 7, 2026, [https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Core-54/out/en/host/generic-attribute-profile--gatt-.html](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Core-54/out/en/host/generic-attribute-profile--gatt-.html)  
23. Boot loop on ESP32-C3 · Issue \#622 · platformio/platform-espressif32 \- GitHub, accessed April 7, 2026, [https://github.com/platformio/platform-espressif32/issues/622](https://github.com/platformio/platform-espressif32/issues/622)  
24. BitChat protocol security report · Issue \#376 \- GitHub, accessed April 7, 2026, [https://github.com/permissionlesstech/bitchat/issues/376](https://github.com/permissionlesstech/bitchat/issues/376)  
25. Noise Protocol Framework \- Wikipedia, accessed April 7, 2026, [https://en.wikipedia.org/wiki/Noise\_Protocol\_Framework](https://en.wikipedia.org/wiki/Noise_Protocol_Framework)  
26. specs/noise/README.md at master · libp2p/specs \- GitHub, accessed April 7, 2026, [https://github.com/libp2p/specs/blob/master/noise/README.md](https://github.com/libp2p/specs/blob/master/noise/README.md)  
27. Using the Noise Protocol Framework to design a distributed capability system, accessed April 7, 2026, [https://www.cloudatomiclab.com/noise-capabilities/](https://www.cloudatomiclab.com/noise-capabilities/)  
28. Every hardware cryptography scheme wolfSSL has ever enabled, accessed April 7, 2026, [https://www.wolfssl.com/every-hardware-cryptography-scheme-wolfssl-has-ever-enabled-2/](https://www.wolfssl.com/every-hardware-cryptography-scheme-wolfssl-has-ever-enabled-2/)  
29. The Noise Protocol Framework, accessed April 7, 2026, [https://noiseprotocol.org/noise.html](https://noiseprotocol.org/noise.html)  
30. Proceedings of the Seminar Innovative Internet Technologies and Mobile Communications (IITM), Winter Semester 2020/2021 \- Chair of Network Architectures and Services \- TUM, accessed April 7, 2026, [https://www.net.in.tum.de/fileadmin/TUM/NET/NET-2021-05-1.pdf](https://www.net.in.tum.de/fileadmin/TUM/NET/NET-2021-05-1.pdf)  
31. A comparison of wireless protocols for battery powered embedded devices \- marending.dev, accessed April 7, 2026, [https://www.marending.dev/notes/esp-protocol/](https://www.marending.dev/notes/esp-protocol/)  
32. XIAO ESP32-S3 vs ESP32-C3 vs ESP32-C6: Which One Is Best for Your Project?, accessed April 7, 2026, [https://www.seeedstudio.com/blog/2026/01/14/xiao-esp32-s3-c3-c6-comparison/](https://www.seeedstudio.com/blog/2026/01/14/xiao-esp32-s3-c3-c6-comparison/)  
33. RSA peripheral 50% slower on ESP32-S3/C3 than S2?, accessed April 7, 2026, [https://esp32.com/viewtopic.php?t=23830](https://esp32.com/viewtopic.php?t=23830)  
34. Espressif RISC-V Hardware Accelerated Cryptographic Functions Up to 1000% Faster than Software \- wolfSSL, accessed April 7, 2026, [https://www.wolfssl.com/espressif-risc-v-hardware-accelerated-cryptographic-functions-up-to-1000-faster-than-software/](https://www.wolfssl.com/espressif-risc-v-hardware-accelerated-cryptographic-functions-up-to-1000-faster-than-software/)  
35. ESP32-C3 Crypto Benchmark | SSL TLS SSH IPsec TCP \- ORYX EMBEDDED, accessed April 7, 2026, [https://www.oryx-embedded.com/benchmark/espressif/crypto-esp32-c3.html](https://www.oryx-embedded.com/benchmark/espressif/crypto-esp32-c3.html)  
36. Offline Messaging Reinvented with Bitchat \- DEV Community, accessed April 7, 2026, [https://dev.to/grenishrai/offline-messaging-reinvented-with-bitchat-5011](https://dev.to/grenishrai/offline-messaging-reinvented-with-bitchat-5011)  
37. How do I choose a UUID for my custom services and characteristics? \- Novel Bits, accessed April 7, 2026, [https://novelbits.io/uuid-for-custom-services-and-characteristics/](https://novelbits.io/uuid-for-custom-services-and-characteristics/)  
38. Introduction \- ESP32-C3 \- — ESP-IDF Programming Guide v6.0 documentation, accessed April 7, 2026, [https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/api-guides/ble/get-started/ble-introduction.html](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/api-guides/ble/get-started/ble-introduction.html)  
39. ervan0707/bitchat-web: bluetooth mesh chat, IRC vibes \- GitHub, accessed April 7, 2026, [https://github.com/ervan0707/bitchat-web](https://github.com/ervan0707/bitchat-web)  
40. BLE service is not found by UUID (the UUID is correct in the code) \- Stack Overflow, accessed April 7, 2026, [https://stackoverflow.com/questions/79919646/ble-service-is-not-found-by-uuid-the-uuid-is-correct-in-the-code](https://stackoverflow.com/questions/79919646/ble-service-is-not-found-by-uuid-the-uuid-is-correct-in-the-code)  
41. App crash when bluetooth is enabled · Issue \#382 · permissionlesstech/bitchat-android, accessed April 7, 2026, [https://github.com/permissionlesstech/bitchat-android/issues/382](https://github.com/permissionlesstech/bitchat-android/issues/382)  
42. BLE Sniffer Basics \+ Comparison Guide (Updated 2026\) \- Novel Bits, accessed April 7, 2026, [https://novelbits.io/bluetooth-low-energy-ble-sniffer-tutorial/](https://novelbits.io/bluetooth-low-energy-ble-sniffer-tutorial/)  
43. Sniffing Bluetooth LE packets \- Nordic Developer Academy, accessed April 7, 2026, [https://academy.nordicsemi.com/courses/bluetooth-low-energy-fundamentals/lessons/lesson-6-bluetooth-le-sniffer/topic/sniffing-bluetooth-le-packets/](https://academy.nordicsemi.com/courses/bluetooth-low-energy-fundamentals/lessons/lesson-6-bluetooth-le-sniffer/topic/sniffing-bluetooth-le-packets/)  
44. Bluetooth Sniffer · justcallmekoko/ESP32Marauder Wiki \- GitHub, accessed April 7, 2026, [https://github.com/justcallmekoko/ESP32Marauder/wiki/bluetooth-sniffer](https://github.com/justcallmekoko/ESP32Marauder/wiki/bluetooth-sniffer)  
45. Can you make ESP32-S3 sniff out ALL BLE packets in the air? \- Reddit, accessed April 7, 2026, [https://www.reddit.com/r/esp32/comments/1qtlqmd/can\_you\_make\_esp32s3\_sniff\_out\_all\_ble\_packets\_in/](https://www.reddit.com/r/esp32/comments/1qtlqmd/can_you_make_esp32s3_sniff_out_all_ble_packets_in/)  
46. Ellisys Bluetooth Tracker Ultra-Portable BLE and Wi-Fi Protocol Analyzer, accessed April 7, 2026, [https://www.ellisys.com/products/btr1/index.php](https://www.ellisys.com/products/btr1/index.php)  
47. Bitchat download | SourceForge.net, accessed April 7, 2026, [https://sourceforge.net/projects/bitchat.mirror/](https://sourceforge.net/projects/bitchat.mirror/)  
48. Why Bitchat Is a Bad Idea: (My Audit Found Critical Zero-Days) | by Saad Khalid, accessed April 7, 2026, [https://saadkhalidhere.medium.com/why-bitchat-is-a-bad-idea-my-audit-found-critical-zero-days-1b126a45a2c5](https://saadkhalidhere.medium.com/why-bitchat-is-a-bad-idea-my-audit-found-critical-zero-days-1b126a45a2c5)  
49. CKB light client for ESP32 — Arduino/PlatformIO, modular transport (WiFi/LoRa/LoRaWAN), BitChat mesh relay, Noise encryption \- GitHub, accessed April 7, 2026, [https://github.com/toastmanAu/ckb-light-esp](https://github.com/toastmanAu/ckb-light-esp)  
50. Bitchat Integration for MeshCore \- Privacy. Cryptography. Freedom., accessed April 7, 2026, [https://www.eddieoz.com/bitchat-integration-for-meshcore/](https://www.eddieoz.com/bitchat-integration-for-meshcore/)

[image1]: <data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAABIAAAAaCAYAAAC6nQw6AAAA0klEQVR4Xu2QMQ4BQRSGR0QlLoBSReMAEo1CJdEpxRFENCIijqCSKFxFJC6g0uppNRL+f/fteJ4pFYr5ki/L9yazM+tcJPI7mnADi/K/BOdwDHPZIjCFO1hRzVOAB9iDT7hy6WKykFaHZ5iHZWlVWePZy3Po0gXL9yg5GdtFNcI2MS25AuEbuUAzCjSeiq1huofDo2k8id2IJ7btAw67gZZdXberaZ6BC7+FrR1oHfl91wNyct8bhb5PS7UZrKlZwgOuTdvCm2mE1+JmfTuIRP6GF+x9LiQOhw8jAAAAAElFTkSuQmCC>

[image2]: <data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAsAAAAbCAYAAACqenW9AAAAo0lEQVR4XmNgGNrgKhD/AeL/QMyJJocV7GWAKCYKgBSSpHg6uiA2IMUAUSyBLoENzGJAdUIbED9FE4MDZPceAmI+IF6FJIYCQIJzgfgSELNCxeYB8T24CiiQZECYnIMmhwEiGCAKQREDovegSqOC6wyobgOxpyDxUQBI8hoafyWU/RFJHAxAkmFo/GwgZgTiY0jiDGJQSWTgBxX7gCY+CgYbAADqfCrdk3T3XwAAAABJRU5ErkJggg==>

[image3]: <data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAoAAAAZCAYAAAAIcL+IAAAAdElEQVR4XmNgGAW0AolAvAyIbdAlkMF/IFaAsiuBuAohhQD7gPgEEh+kqROJDwefGCCS84FYFk0OBWgyQBTC8HtUaQiYC8QdSHw5BohiDAAS3I7EV4eKYYDvQJwPxEIMkOABKQKxsQIeII4AYjV0iVFAPQAAyIQVuMnEzLsAAAAASUVORK5CYII=>

[image4]: <data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAoAAAAZCAYAAAAIcL+IAAAAfUlEQVR4XmNgGAW0At5AvB6I7dElYKAGiP8DsSOU3wHEExHSEABTxIQkJgzEx5D4YABSdBXKZgXiNKgYCgC5CSS4FojbgDiDAWIaBkhhwKIbG+BjgCiUQhPnB+JvaGIMu4H4NQNEUgWItwDxAmQFyIANiKOBWAZdYhRQDwAAoscT1AKA7AAAAAAASUVORK5CYII=>

[image5]: <data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAABMAAAAaCAYAAABVX2cEAAAAzUlEQVR4XmNgGAWjgH7AAIj7gJgVyk8C4g6ENApQZoCoLQFidjQ5BjYgPgzEIUD8H4i/A7EUA8RAEB8ZLADiQ0h8dHmG/VA6kwEiKQPlg9i3oGwYQNbcisYHg1oofY8BVZIDiQ0CTAwQ+fdAHIMmhwFACg+gC6KBdwwQdTAsjyqNACBJe3RBJMCIxE5ngKh/gyQGBxEMWPyPBCYwQOSRDbwPxJOQ+HBwmQG/YdeAeD0Sn48Bj/o/QDwFXRAN3GVAhNUNIGZGlR4Fo2AIAgABrixyLiVf7gAAAABJRU5ErkJggg==>

[image6]: <data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAABMAAAAaCAYAAABVX2cEAAAAz0lEQVR4XmNgGAWjgH7AAIj7gJgVyk8C4g6ENArgB+JuIG4EYjY0ObDAYSAOAeL/QPwdiKUYIAaC+DDABMTfgHgbA0QPM5o8GOyH0pkMEEkZKB/EvgVlw/gXkfgwMRRQC6XvMaBKciCxFzOgyqkB8V0gbkMSQwEgxQfQBaEAJAfCk4C4EogdUGSxAJBie3RBKADJfUYXxAUiGLD4HwmA5C6jCwIBN7oACIAU4jMsjwFT3gaI36OJgcEfIJ6CLogGyhkQYQcyBGTYKBgFQxsAAGULK5Fg/D5dAAAAAElFTkSuQmCC>

[image7]: <data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAwAAAAaCAYAAACD+r1hAAAAmklEQVR4XmNgGPSAEYhV0QVxgXdA/B+KiQaXGUjUAFJ8EV0QHwBp8EcXxAWCGBDOAWlaBcTRCGlMAHIKSMMfINaEioH4y+Aq0AC2EPqLRQwOQBJhWMQOoomBgR8DpkmgSASJmaOJg8FpBkwNs7GIwQFI4ioWsVtQ9ktkCRAASQZjEfMCYlYg3oIsIQaVRAeLGCDi59AlRsEQBAAthienVaah6AAAAABJRU5ErkJggg==>

[image8]: <data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAABQAAAAaCAYAAAC3g3x9AAAA+klEQVR4XmNgGAVEAGkgFkYXJAesBeL/UOyHJkc2CGGAGEg1cI2BygaCDLuBLkgI+APxOiBORJdggBgYgcSPBOJWJD4K8GSAaHCE8qcC8VmENFgzsnc/A7EoEB9jgIQtCnBlgCiWQxID8U8j8W9CxUDgE5RugIpJQPlwABJ8hSamiMYHqQEZ9AJNnBONz2DGAFHsiy6BBkBqLkDpD0DMgiqNAHMZCCeFcAZUNX/R+CgglgG3ZAyUvsyAquYlEr8ciCWR5MAAJKmBJvYOiN2hbJA8KFHDwEMgfo0khwGkgPgPA0QShFehSoPFdJD4XFAxEMaIlFEwCoYdAAAICjzCMzfx3QAAAABJRU5ErkJggg==>