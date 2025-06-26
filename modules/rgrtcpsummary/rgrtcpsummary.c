/**
 * @file rgrtcpsummary.c RG Nets RTCP summary module
 * Output RTCP stats and MOS score at the end of a call if there are any
 *
 *  Copyright (C) 2025 Michael D. Ketchel
 */
#include <re.h>
#include <baresip.h>


/* Clamp value between min and max */
static double clamp(double value, double min, double max) {
	if (value < min) return min;
	if (value > max) return max;
	return value;
}

/* Calculate MOS using ITU-T E-model approximation */
static double calculate_mos(double rtt_ms, double tx_jitter_ms,
			double rx_jitter_ms, double packet_loss_percent) {
	/* Step 1: Calculate mouth-to-ear delay (ms) */
	double delay = rtt_ms + tx_jitter_ms + rx_jitter_ms;

	/* Step 2: Calculate delay impairment */
	double delay_impairment = 0.024 * delay;
	if (delay > 177.3) {
		delay_impairment += 0.11 * (delay - 177.3);
	}

	/* Step 3: Calculate loss impairment (simplified) */
	double loss_impairment = 2.5 * packet_loss_percent;

	/* Step 4: Calculate R-factor */
	double R = 94.2 - delay_impairment - loss_impairment;
	R = clamp(R, 0.0, 100.0);

	/* Step 5: Calculate MOS from R-factor */
	double MOS = 1.0 + 0.035 * R + (R * (R - 60.0) * (100.0 - R) * 7.0e-6);
	MOS = clamp(MOS, 1.0, 5.0);

	return MOS;
}


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
			 	1.0 * rtcp->rtt/1000,
			 	1.0 * rtcp->tx.jit/1000,
			 	1.0 * rtcp->rx.jit/1000,
				/* A naive handling of packet loss here.
				   There is likely a better way. */
				(1.0*(rtcp->rx.lost + rtcp->tx.lost)
					/(rtcp->rx.sent + rtcp->tx.sent))
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
