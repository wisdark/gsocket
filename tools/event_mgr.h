#ifndef __GS_EVENT_MGR_H__
#define __GS_EVENT_MGR_H__ 1

#ifdef DEBUG
#define GS_APP_KEEPALIVE	GS_SEC_TO_USEC(10) // If no activty send app-layer ping (-i needed)
#else
#define GS_APP_KEEPALIVE	GS_SEC_TO_USEC(60 * 2) // If no activty send app-layer ping (-i needed)
#endif
#define GS_APP_PINGFREQ		GS_SEC_TO_USEC(5)      // Send a ping every 5 sec (-i, in console)
#define GS_APP_BPSFREQ		GS_SEC_TO_USEC(1)      // Calculate BPS every second
#define GS_APP_IDSFREQ		GS_SEC_TO_USEC(5)      // Check for other user active on system

int cbe_ping(void *ptr);
int cbe_bps(void *ptr);
int cbe_ids(void *ptr);

#endif /* !__GS_EVENT_MGR_H__ */