/**
 * @file rgrtcpsummary.c RG Nets RTCP summary module
 * Output RTCP stats and MOS score at the end of a call if there are any
 *
 *  Copyright (C) 2025 Michael D. Ketchel
 */
#include <re.h>
#include <baresip.h>
#include <math.h>


/* Clamp value between min and max */
static double clamp(double value, double min, double max) {
	if (value < min) return min;
	if (value > max) return max;
	return value;
}

/* Improved MOS calculation based on ITU-T E-model with realistic thresholds */
static double calculate_mos(uint32_t packets_tx, uint32_t packets_rx,
			   uint32_t lost_tx, uint32_t lost_rx,
			   uint32_t disc_tx, uint32_t disc_rx,
			   double rtt_ms, double tx_jitter_ms, double rx_jitter_ms) {
	
	/* Calculate effective packet loss rates for both directions */
	double rx_loss_rate = 0.0;
	double tx_loss_rate = 0.0;
	
	if (packets_rx > 0) {
		rx_loss_rate = (double)(lost_rx + disc_rx) / packets_rx;
	}
	if (packets_tx > 0) {
		tx_loss_rate = (double)(lost_tx + disc_tx) / packets_tx;
	}
	
	/* Use the worse of the two directions for overall loss */
	double effective_loss_percent = fmax(rx_loss_rate, tx_loss_rate) * 100.0;
	
	/* Calculate one-way delay estimate (RTT/2 + jitter buffer) */
	double avg_jitter = (tx_jitter_ms + rx_jitter_ms) / 2.0;
	double one_way_delay = (rtt_ms / 2.0) + avg_jitter;
	
	/* More aggressive delay impairment - users notice delay more than ITU suggests */
	double delay_impairment = 0.0;
	if (one_way_delay > 150.0) {
		delay_impairment = 0.5 * (one_way_delay - 150.0);
	} else if (one_way_delay > 100.0) {
		delay_impairment = 0.2 * (one_way_delay - 100.0);
	}
	
	/* More realistic packet loss impairment */
	double loss_impairment = 0.0;
	if (effective_loss_percent > 5.0) {
		loss_impairment = 15.0 + 5.0 * (effective_loss_percent - 5.0);
	} else if (effective_loss_percent > 1.0) {
		loss_impairment = 5.0 + 2.5 * (effective_loss_percent - 1.0);
	} else if (effective_loss_percent > 0.1) {
		loss_impairment = 2.0 * effective_loss_percent;
	}
	
	/* Jitter impairment - high jitter severely impacts quality */
	double jitter_impairment = 0.0;
	if (avg_jitter > 50.0) {
		jitter_impairment = 10.0 + 0.5 * (avg_jitter - 50.0);
	} else if (avg_jitter > 20.0) {
		jitter_impairment = 0.3 * (avg_jitter - 20.0);
	}
	
	/* Start with a lower baseline R-factor for realistic scoring */
	double R = 85.0 - delay_impairment - loss_impairment - jitter_impairment;
	R = clamp(R, 0.0, 100.0);
	
	/* Convert R-factor to MOS with more realistic mapping */
	double MOS;
	if (R >= 80.0) {
		MOS = 4.0 + 0.025 * (R - 80.0);  /* 4.0-4.5 range */
	} else if (R >= 60.0) {
		MOS = 3.0 + 0.05 * (R - 60.0);   /* 3.0-4.0 range */
	} else if (R >= 40.0) {
		MOS = 2.5 + 0.025 * (R - 40.0);  /* 2.5-3.0 range */
	} else if (R >= 20.0) {
		MOS = 2.0 + 0.025 * (R - 20.0);  /* 2.0-2.5 range */
	} else {
		MOS = 1.0 + 0.05 * R;            /* 1.0-2.0 range */
	}
	
	return clamp(MOS, 1.0, 5.0);
}
// Jul 28 14:46:30 wlanpi-475 python3[759]: 2025-07-28 14:46:30,604 |     INFO | wlanpi_rxg_agent.lib.sip_control.sip_test_baresip: Received RTCP summary: {'EX': 'BareSip', 'CS': '0', 'CD': '40', 'PR': '1984', 'PS': '3731', 'PL': '14,1', 'PD': '0,0', 'JI': '2.6,9.7', 'DL': '43.3', 'IP': '0.0.0.0:28998,192.168.7.15:10172', 'MOS': '4.40'}
// Jul 28 14:56:31 wlanpi-475 python3[759]: 2025-07-28 14:56:31,146 |     INFO | wlanpi_rxg_agent.lib.sip_control.sip_test_baresip: Received RTCP summary: {'EX': 'BareSip', 'CS': '0', 'CD': '40', 'PR': '1991', 'PS': '3896', 'PL': '7,5', 'PD': '0,0', 'JI': '1.0,9.7', 'DL': '3.2', 'IP': '0.0.0.0:17130,192.168.7.15:10120', 'MOS': '4.42'} (sip_test_baresip.py:104)
//
//
// packets TX, packets RX, packets lost TX, packets lost RX, packets discorded TX, packets discorded RX, jitter rx, jitter tx, and rttt

static void print_rtcp_summary_line(const struct call *call,
				    const struct stream *s)
{
	const struct rtcp_stats *rtcp;
	rtcp = stream_rtcp_stats(s);

	if (rtcp && (rtcp->tx.sent || rtcp->rx.sent)) {

		info("\n");
		/*
		 * Add a stats line to make it easier to parse result
		 * from script. Use a similar format used for the
		 * XRTP message in audio.c
		 */
		info(
			"EX=BareSip;"  /* Reporter Identifier */
			"CS=%d;"       /* Call Setup in ms */
			"CD=%d;"       /* Call Duration in sec */
			"PR=%u;"       /* Packets RX */
			"PS=%u;"       /* Packets TX */
			"PL=%d,%d;"    /* Packets Lost RX, TX */
			"PD=%d,%d;"    /* Packets Discarded, RX,TX */
			"JI=%.1f,%.1f;"/* Jitter RX, TX in ms */
			"DL=%.1f;"     /* RTT in ms */
			"IP=%J,%J;"    /* Local, Remote IPs */
			"MOS=%.2f;"    /* MOS score */
			 "\n"
			,
			 call_setup_duration(call) * 1000,
			 call_duration(call),
			 rtcp->rx.sent,
			 rtcp->tx.sent,
			 rtcp->rx.lost,
			 rtcp->tx.lost,
			 stream_metric_get_rx_n_err(s),
			 stream_metric_get_tx_n_err(s),
			 1.0 * rtcp->rx.jit/1000,
			 1.0 * rtcp->tx.jit/1000,
			 1.0 * rtcp->rtt/1000,
			 sdp_media_laddr(stream_sdpmedia(s)),
			 sdp_media_raddr(stream_sdpmedia(s)),
			 calculate_mos(
			 	rtcp->tx.sent,
			 	rtcp->rx.sent,
			 	rtcp->tx.lost,
			 	rtcp->rx.lost,
			 	stream_metric_get_tx_n_err(s),
			 	stream_metric_get_rx_n_err(s),
			 	1.0 * rtcp->rtt/1000,
			 	1.0 * rtcp->tx.jit/1000,
			 	1.0 * rtcp->rx.jit/1000
				)
			 );
	}
	else {
		/*
			* put a line showing how
			* RTCP stats were NOT collected
			*/
		info("\n");
		info("EX=BareSip;ERROR=No RTCP stats collected;\n");
	}
}


static void event_handler(enum ua_event ev, struct bevent *event, void *arg)
{
	const struct stream *s;
	struct le *le;
	struct call *call = bevent_get_call(event);
	(void)arg;

	switch (ev) {

	case UA_EVENT_CALL_CLOSED:
		for (le = call_streaml(call)->head;
		     le;
		     le = le->next) {
			s = le->data;
			print_rtcp_summary_line(call, s);
		}
		break;

	default:
		break;
	}
}


static int module_init(void)
{
	int err = bevent_register(event_handler, NULL);
	if (err) {
		info("Error loading rgrtcpsummary module: %d", err);
		return err;
	}
	return 0;
}


static int module_close(void)
{
	debug("rgrtcpsummary: module closing..\n");
	bevent_unregister(event_handler);
	return 0;
}


const struct mod_export DECL_EXPORTS(rgrtcpsummary) = {
	"rgrtcpsummary",
	"application",
	module_init,
	module_close
};
