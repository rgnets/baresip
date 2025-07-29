#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdint.h>

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

void print_detailed_calculation(uint32_t packets_tx, uint32_t packets_rx,
			       uint32_t lost_tx, uint32_t lost_rx,
			       uint32_t disc_tx, uint32_t disc_rx,
			       double rtt_ms, double tx_jitter_ms, double rx_jitter_ms) {
	
	printf("\n=== MOS Calculation Details ===\n");
	printf("Input Parameters:\n");
	printf("  Packets TX: %u, RX: %u\n", packets_tx, packets_rx);
	printf("  Lost TX: %u, RX: %u\n", lost_tx, lost_rx);
	printf("  Discarded TX: %u, RX: %u\n", disc_tx, disc_rx);
	printf("  RTT: %.1f ms, Jitter TX: %.1f ms, RX: %.1f ms\n", 
	       rtt_ms, tx_jitter_ms, rx_jitter_ms);
	
	/* Calculate components */
	double rx_loss_rate = 0.0;
	double tx_loss_rate = 0.0;
	
	if (packets_rx > 0) {
		rx_loss_rate = (double)(lost_rx + disc_rx) / packets_rx;
	}
	if (packets_tx > 0) {
		tx_loss_rate = (double)(lost_tx + disc_tx) / packets_tx;
	}
	
	double effective_loss_percent = fmax(rx_loss_rate, tx_loss_rate) * 100.0;
	double avg_jitter = (tx_jitter_ms + rx_jitter_ms) / 2.0;
	double one_way_delay = (rtt_ms / 2.0) + avg_jitter;
	
	printf("\nCalculated Metrics:\n");
	printf("  TX Loss Rate: %.2f%%, RX Loss Rate: %.2f%%\n", 
	       tx_loss_rate * 100.0, rx_loss_rate * 100.0);
	printf("  Effective Loss: %.2f%% (worst direction)\n", effective_loss_percent);
	printf("  Average Jitter: %.1f ms\n", avg_jitter);
	printf("  One-way Delay: %.1f ms\n", one_way_delay);
	
	double mos = calculate_mos(packets_tx, packets_rx, lost_tx, lost_rx,
				   disc_tx, disc_rx, rtt_ms, tx_jitter_ms, rx_jitter_ms);
	
	printf("\nFinal MOS Score: %.2f\n", mos);
	printf("===============================\n");
}

int main(int argc, char *argv[]) {
	if (argc == 10) {
		/* Command line arguments */
		uint32_t packets_tx = atoi(argv[1]);
		uint32_t packets_rx = atoi(argv[2]);
		uint32_t lost_tx = atoi(argv[3]);
		uint32_t lost_rx = atoi(argv[4]);
		uint32_t disc_tx = atoi(argv[5]);
		uint32_t disc_rx = atoi(argv[6]);
		double rtt_ms = atof(argv[7]);
		double tx_jitter_ms = atof(argv[8]);
		double rx_jitter_ms = atof(argv[9]);
		
		print_detailed_calculation(packets_tx, packets_rx, lost_tx, lost_rx,
					   disc_tx, disc_rx, rtt_ms, tx_jitter_ms, rx_jitter_ms);
	} else {
		printf("MOS Calculator Test Program\n");
		printf("Usage: %s packets_tx packets_rx lost_tx lost_rx disc_tx disc_rx rtt_ms tx_jitter_ms rx_jitter_ms\n\n", argv[0]);
		
		printf("Testing with your sample data:\n");
		
		/* Test case 1 from your logs */
		printf("\n--- Test Case 1 (from logs) ---");
		print_detailed_calculation(3731, 1984, 1, 14, 0, 0, 43.3, 9.7, 2.6);
		
		/* Test case 2 from your logs */
		printf("\n--- Test Case 2 (from logs) ---");
		print_detailed_calculation(3896, 1991, 5, 7, 0, 0, 3.2, 9.7, 1.0);
		
		/* Perfect call */
		printf("\n--- Perfect Call ---");
		print_detailed_calculation(1000, 1000, 0, 0, 0, 0, 20.0, 1.0, 1.0);
		
		/* Poor quality call */
		printf("\n--- Poor Quality Call ---");
		print_detailed_calculation(1000, 1000, 50, 80, 10, 15, 200.0, 50.0, 60.0);
	}
	
	return 0;
}