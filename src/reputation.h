/**
 * Copyright (c) 2009 Open Information Security Foundation
 *
 * \author Pablo Rincon Crespo <pablo.rincon.crespo@gmail.com>
 * \author Victor Julien <victor@inliniac.net>
 *         Original Idea by Matt Jonkman
 *
 *  General reputation for ip's (ipv4/ipv6) and (maybe later) host names
 */

#ifndef __REPUTATION_H__
#define __REPUTATION_H__

/** Reputation numbers (types) that we can use to lookup/update, etc
 *  Please, dont convert this to a enum since we want the same reputation
 *  codes always. */
#define REPUTATION_SPAM             0   /**< spammer */
#define REPUTATION_CNC              1   /**< CnC server */
#define REPUTATION_SCAN             2   /**< scanner */
#define REPUTATION_HOSTILE          3   /**< hijacked nets, RBN nets, etc */
#define REPUTATION_DYNAMIC          4   /**< Known dial up, residential, user networks */
#define REPUTATION_PUBLICACCESS     5   /**< known internet cafe's open access points */
#define REPUTATION_PROXY            6   /**< known tor out nodes, proxy servers, etc */
#define REPUTATION_P2P              7   /**< Heavy p2p node, torrent server, other sharing services */
#define REPUTATION_UTILITY          8   /**< known good places like google, yahoo, msn.com, etc */
#define REPUTATION_DDOS             9   /**< Known ddos participant */
#define REPUTATION_PHISH            10  /**< Known Phishing site */
#define REPUTATION_MALWARE          11  /**< Known Malware distribution site. Hacked web server, etc */
#define REPUTATION_ZOMBIE           12  /**< Known Zombie (botnet member) They typically are Scanner or Hostile,
                                             but if collaboration with botnet snooping, like we did back in
                                             2005 or so, can proactively identify online zombies that joined a
                                             botnet, you may want to break those out separately */
#define REPUTATION_NUMBER           13  /**< number of rep types we have for data structure size (be careful with this) */


/* Flags for reputation */
#define REPUTATION_FLAG_NEEDSYNC    0x01 /**< rep was changed by engine, needs sync with external hub */

/** Reputation Context for IPV4 IPV6 */
typedef struct IPReputationCtx_ {
    /** Radix trees that holds the host reputation information */
    SCRadixTree *reputationIPV4_tree;
    SCRadixTree *reputationIPV6_tree;

    /** Mutex to support concurrent access */
    SCMutex reputationIPV4_lock;
    SCMutex reputationIPV6_lock;
}IPReputationCtx;

/** Reputation Data */
//TODO: Add a timestamp here to know the last update of this reputation.
typedef struct Reputation_ {
    uint8_t reps[REPUTATION_NUMBER]; /**< array of 8 bit reputations */
    uint8_t flags; /**< reputation flags */
    time_t ctime; /**< creation time (epoch) */
    time_t mtime; /**< modification time (epoch) */
} Reputation;

/* flags for transactions */
#define TRANSACTION_FLAG_NEEDSYNC 0x01 /**< We will apply the transaction only if necesary */
#define TRANSACTION_FLAG_INCS     0x02 /**< We will increment only if necesary */
#define TRANSACTION_FLAG_DECS     0x03 /**< We will decrement only if necesary */

/* transaction for feedback */
typedef struct ReputationTransaction_ {
    uint16_t inc[REPUTATION_NUMBER];
    uint16_t dec[REPUTATION_NUMBER];
    uint8_t flags;
}ReputationTransaction;

/* API */
Reputation *SCReputationAllocData();
Reputation *SCReputationClone(Reputation *);
void SCReputationFreeData(void *);

IPReputationCtx *SCReputationInitCtx();
void SCReputationFreeCtx();

void SCReputationPrint(Reputation *);
void SCReputationRegisterTests(void);

#endif /* __REPUTATION_H__ */
