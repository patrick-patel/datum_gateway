# DATUM Gateway
**Decentralized Alternative Templates for Universal Mining**
(c) 2024 Bitcoin Ocean, LLC & Jason Hughes

The DATUM Gateway implements lightweight efficient client side decentralized block template creation for true solo mining.

It reaches out to a local Bitcoin node for block templates, generates and distributes work for mining hardware, and submits solved blocks to the network directly.

For miners wanting to pool rewards, it facilitates communication with a DATUM-supporting pool in addition to the above.  The pool is responsible for coordinating the block reward split based on work done for the pool by the miner, but does not create work for the miner.

The work provided by the gateway to mining hardware is generated only from the local node generating templates for the miner. The real miner is always whoever is running the Bitcoin node. With DATUM, that's not the pool. As the protocol is intended solely for mining of decentralized block templates, the DATUM protocol has no mechanisms for the pool providing the information needed to construct work or a block template.

Currently the DATUM Gateway supports communication with mining hardware using the Stratum v1 protocol with version rolling extensions (aka "ASICBoost").  Communication with the Bitcoin node is via RPC and must support GBT ("getblocktemplate").  Finally, communication with the pool is via the DATUM protocol.

**Using Bitcoin Knots is highly recommended**. This gives miners fine controls over how they wish to construct their block templates.  Other node implementations that support GBT can also be used.  This includes Bitcoin Core, but it is severely lacking in template control options.  That is unfortunately a centralizing force which partly defeats the purpose of decentralizing block template creation in the first place.

The DATUM Gateway only supports mining Bitcoin.  Modifying the code to support non-Bitcoin is not straightforward, as many optimizations and design considerations are tightly tied to Bitcoin-specific restraints for efficiency.

## DATUM Protocol
The DATUM Gateway's communication with the mining pool is via the DATUM Protocol.  This is an encrypted communication link between the DATUM Gateway (client) and DATUM Prime (pool side).

The protocol itself was made from the ground up as a custom protocol.  Its specification is evolving, subject to change, and will be published elsewhere.

The core concepts of the protocol:

 - Encrypt communications between the Gateway and pool
 - Obfuscate the communications somewhat so a MITM is unable to glean useful or accurate insight into the miner's operation via analysis of the still-ciphered communications.
 - Retrieve proper generation transaction payout splits from the pool for locally constructed templates
 - Submit work to the pool with sufficient data to efficiently validate and accept the work for proper rewards
 - Communicate minimal guardrails and requirements for a valid template to earn pooled rewards

With the current version of the protocol, the pool does block validation after coordinating with the miner. This is strictly to ensure miners are not accidentally creating invalid blocks while DATUM is still undergoing testing. In a future version of the protocol, the pool will not be in charge of this function and will be almost completely blinded to the contents of the miner's block template.

The protocol is not specific to a pooled reward system, as the Gateway coordinates the appropriate generation transaction with the pool.  However, in the spirit of maximum decentralization, the pool should implement rewarding miners directly from generated payouts, such as with OCEAN's TIDES reward system.

![DATUM v0 2-beta recommended setup - network diagram](doc/DATUM_recommended_setup-network_diagram.svg)

## Requirements

 - 64-bit AMD or Intel system. Other systems may work, but at this time it is at your own risk.
 - Linux-based operating system. Other OSs will be supported in the future.
 - Bitcoin full node ([Bitcoin Knots](https://bitcoinknots.org/) recommended) fully synced with the Bitcoin network.
 - Fast storage recommended for the Bitcoin node.
 - Stable internet connection for both the Bitcoin node and Gateway's communication with the pool.
 - CPU powerful enough to run the Bitcoin node without validation delays.
 - Approximately 1GB/RAM, plus 1GB/RAM per 1000 Stratum clients, plus Bitcoin node RAM requirements.
 - Bitcoin mining hardware able to reach the system running the DATUM Gateway.

This list is not extensive, but the main goal is the have a stable system for your Bitcoin node and the Gateway such that your node is processing new incoming blocks and getting templates to the Gateway as quickly as possible.  While this may all work on relatively low end hardware, your mileage may vary.

No modifications to the Bitcoin node source code is required for the Gateway, as it uses the standard GBT mechanism for template fetch.

The following external libraries are required:
 - libcurl
 - libjansson
 - libmicrohttpd
 - libsodium

## Node Configuration
Your Bitcoin node must be configured to construct blocks as you desire.  Bitcoin Knots provides many options for configuring your node's policy and is highly recommended.

At this time, you must also reserve some block space for the pool's generation transaction.  The following options are currently recommended:

    blockmaxsize=3985000
    blockmaxweight=3985000

Note: This reservation requirement will be removed for Bitcoin Knots users in a future version of the DATUM Gateway thanks to support for on-the-fly specification of these metrics by the client in Knots (as of version 27.1).

You must also configure a "*blocknotify*" setting to alert the Gateway of new blocks.  See Installation section.

Finally, the Gateway must have RPC access to your node, and you must add an RPC user to your configuration to facilitate this, as well as ensuring the service running the Gateway is whitelisted for RPC access (if not on the same machine).

Some additional recommendations:

    maxmempool=1000
    blockreconstructionextratxn=1000000

As a true miner, you'll most likely want as many valid transactions as possible in your mempool which meet your node's policies.

## Installation
Install and fully sync your Bitcoin full node. Instructions for this are beyond the scope of this document.

Configure your node to create block templates as you desire. Be sure to reserve some space for the generation transaction, otherwise your work will not be able to fit a reward split.  See node configuration recommendations above.

Install the required libraries and development packages for dependencies: cmake, pkg-config, jansson, libmicrohttpd, libsodium, and libcurl.

For Debian/Ubuntu it would be:

    sudo apt install cmake pkgconf libcurl4-openssl-dev libjansson-dev libmicrohttpd-dev libsodium-dev psmisc

For RedHat/Fedora it would be:

    sudo dnf install cmake pkgconf libcurl-devel jansson-devel libmicrohttpd-devel libsodium-devel psmisc

Compile DATUM by running:

    cmake . && make

Run the datum_gateway executable with the -? flag for detailed configuration information, descriptions, and required options.  Then construct a configuration file (defaults to "datum_gateway_config.json" in the current working directory). Be sure to also set your coinbase tags.  The primary tag setting is unused in pooled mining, however the secondary tag is intended to show on things like block explorers when you mine a block.

To avoid mining stale work, you will need to ensure the DATUM Gateway receives new block notifications from your node. It is suggested you run the DATUM Gateway as the same user as your full node and utilize the following configuration line in your bitcoin.conf:

    blocknotify=killall -USR1 datum_gateway

Ensure you have "killall" installed on your system (*psmisc* package on Debian-like OSs).

If the node and Gateway are on different systems, you may need to utilize the "NOTIFY" endpoint on the Gateway's dashboard/API instead.

## Template/Share Requirements for Pooled Mining

 - Must be a valid block and conform to current Bitcoin consensus rules
 - Submitted work must be for the current latest block height, valid time, etc
 - Must include generation transaction outputs provided by the pool in the order provided
 - Must include the primary coinbase tag as provided by the pool
 - Must include the unique identifier provided by the pool
 - Work must include the work target and meet/exceed that target
 - Any additional requirements by pool documentation

## Notes/Known Issues/Limitations

- By default, if the connection with the pool is lost and fails to reconnect, the Gateway will disconnect all stratum clients. This way miners can use their built-in failover and switch to non-DATUM mining, or an alternate/backup Gateway.
- Accepted/rejected share counts on mining hardware may not perfectly match with the pool. The delta may vary depending on the Gateway's configuration. This is because shares are first accepted or rejected as valid for your local template based on your local node, and then again accepted or rejected based on the pool's requirements, latency to the pool (stale work), latency between your node and the network (stale work), etc.  Stratum v1 has no mechanism to report back to the miner that previously accepted work is now rejected, and it doesn't make sense to wait for the pool before responding, either.

**Most importantly**, please note that this is currently a public **BETA** release. While best efforts have been made to ensure this software is as stable and as useful as possible, you may still encounter issues.

This software is likely to undergo rapid development and revisions up until a v1.0 stable release. Some of these revisions may include changes, such as protocol changes, that require upgrading to the latest version with short or even no notice in order to continue using the software with a DATUM pool. Be sure to watch for important updates!

Be sure you have failover settings on your miners. As a best practice, when mining on a DATUM pool, set your miner's failover to use that pool's Stratum endpoint.

## License

The DATUM Gateway (including the DATUM Protocol) is free open source software and released under the terms of the MIT license.  See LICENSE.

## Address-Based Split Feature

DATUM Gateway allows for address-based share splitting where incoming mining shares from a specific address can be automatically distributed to multiple recipient addresses based on configured percentages. This feature is an alternative to the username-based splitting mechanism.

### Setting Up Address Splits

1. Add the path to your address split configuration file in your gateway configuration:
   ```json
   {
     "mining": {
       "address_split_json": "/path/to/address_splits.json"
     }
   }
   ```

2. Create the JSON configuration file with your split definitions:
   ```json
   {
     "address_splits": {
       "bc1qsourceaddress": [
         {
           "address": "bc1qrecipient1",
           "percentage": 60
         },
         {
           "address": "bc1qrecipient2",
           "percentage": 40
         }
       ]
     }
   }
   ```

3. The gateway will load this configuration on startup. When mining rewards are earned by "bc1qsourceaddress", they will be automatically split with 60% going to "bc1qrecipient1" and 40% to "bc1qrecipient2".

### Dynamic Configuration Updates

The configuration can be updated without restarting the gateway:
- The file is automatically reloaded when modified
- You can manually trigger a reload via the web interface or the API endpoint `/api/address_split/reload`

This feature is useful for managing share distributions in mining operations with multiple participants without requiring miners to configure complex usernames.

## Key Features

- Stratum server interface for Bitcoin miners
- Connection to DATUM Pool for decentralized mining
- Username-based share distribution
- Address-based share distribution (NEW)
- Automatic difficulty adjustment
- Web-based administration interface

## Address-Based Split Feature (NEW)

The address-based split feature allows you to define percentage-based distributions for incoming mining shares based on the source Bitcoin address. Unlike the username-based split that requires miners to set up specific percentage split formatting, this feature configures splits on the gateway side using a JSON configuration file.

### Benefits

- Configure splits without requiring miners to modify their usernames
- Update split configurations without gateway restarts
- Support unlimited recipients per source address
- High-performance lookup with hash table implementation
- Automatic detection of configuration file changes

### Configuration

1. In your DATUM Gateway configuration file, set the path to the address split JSON file:

```json
{
  "mining": {
    "address_split_json": "/path/to/address_splits.json"
  }
}
```

2. Create the address splits JSON file with the following structure:

```json
{
  "address_splits": {
    "bc1qsourceaddress1": [
      {
        "address": "bc1qrecipient1",
        "percentage": 50
      },
      {
        "address": "bc1qrecipient2",
        "percentage": 30
      },
      {
        "address": "bc1qrecipient3",
        "percentage": 20
      }
    ],
    "bc1qsourceaddress2": [
      {
        "address": "bc1qrecipient4",
        "percentage": 60
      },
      {
        "address": "bc1qrecipient5",
        "percentage": 40
      }
    ]
  }
}
```

### How It Works

1. Each miner submits shares using their Bitcoin address as the username
2. When a share is received, DATUM Gateway checks if the address has a defined split configuration
3. If a split is defined, a recipient is selected based on the configured percentages
4. The share is credited to the selected recipient address
5. If no split is defined, the original address receives the credit

### Reloading Configuration

The configuration can be reloaded in two ways without restarting the gateway:

1. **Automatic detection**: The gateway checks for file modifications and automatically reloads the configuration
2. **API endpoint**: You can trigger a manual reload via the web interface or by calling the API endpoint:
   ```
   GET /api/address_split/reload
   ```

### Example Setup

To route 50% of shares from address `bc1qexample` to address `bc1qrecipient1` and 50% to address `bc1qrecipient2`:

1. Create a JSON file with this configuration:
   ```json
   {
     "address_splits": {
       "bc1qexample": [
         {
           "address": "bc1qrecipient1",
           "percentage": 50
         },
         {
           "address": "bc1qrecipient2",
           "percentage": 50
         }
       ]
     }
   }
   ```

2. Set the path to this file in your DATUM Gateway configuration
3. Restart DATUM Gateway or use the reload API endpoint

### Notes

- All Bitcoin addresses (both source and recipients) are validated
- Percentages must add up to 100% (small variations are automatically normalized)
- The feature falls back to the existing username split functionality when no address split is configured
- Configuration changes are applied immediately without requiring a gateway restart
- You can view and change the configuration file path in the web interface under Advanced settings
