#ifndef TIME_RECEIVER_H
#define TIME_RECEIVER_H

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/steady_timer.hpp>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace asio = lslboost::asio;
using asio::ip::udp;
using lslboost::system::error_code;

namespace lsl {
class inlet_connection;
class api_config;

/// list of time estimates with error bounds
typedef std::vector<std::pair<double, double>> estimate_list;

/**
 * Internal class of an inlet that's responsible for retrieving time-correction data of the inlet.
 * The actual communication runs in an internal background thread, while the public function
 * (time_correction()) waits for the thread to finish.
 * The public function has an optional timeout after which it gives up, while the background thread
 * continues to do its job (so the next public-function call may succeed within the timeout).
 * The background thread terminates only if the time_receiver is destroyed or the underlying
 * connection is lost or shut down.
 */
class time_receiver {
public:
	/// Construct a new time receiver for a given connection.
	time_receiver(inlet_connection &conn);

	/// Destructor. Stops the background activities.
	~time_receiver();

	/**
	 * Retrieve an estimated time correction offset for the given stream.
	 *
	 * The first call to this function takes several msec for an initial estimate, subsequent calls
	 * are instantaneous.
	 * The correction offset is periodically re-estimated in the background (once every few sec.).
	 * @param remote_time Time of this measurment on remote computer
	 * @param uncertainty Maximum uncertainty of this measurement (maps to round-trip-time).
	 * @param timeout Timeout for first time-correction estimate.
	 * @return The time correction estimate.
	 * @throws timeout_error If the initial estimate times out.
	 */
	double time_correction(double timeout = 2);
	double time_correction(double *remote_time, double *uncertainty, double timeout);

	/**
	 * Determine whether the clock was (potentially) reset since the last call to was_reset()
	 *
	 * This can happen if the stream got lost (e.g., app crash) and the computer got restarted or
	 * swapped out
	 */
	bool was_reset();

private:
	/// The time reader / updater thread.
	void time_thread();

	/// Start a new multi-packet exchange for time estimation
	void start_time_estimation();

	/// Send the next packet in an exchange
	void send_next_packet(int packet_num);

	/// Request reception of the next time packet
	void receive_next_packet();

	/// Handler that gets called once reception of a time packet has completed
	void handle_receive_outcome(error_code err, std::size_t len);

	/// Handlers that gets called once the time estimation results shall be aggregated.
	void result_aggregation_scheduled(error_code err);

	/// Ensures that the time-offset is reset when the underlying connection is recovered (e.g.,
	/// switches to another host)
	void reset_timeoffset_on_recovery();

	/// the underlying connection
	inlet_connection &conn_;

	// background reader thread and the data generated by it
	/// updates time offset
	std::thread time_thread_;
	/// whether the clock was reset
	bool was_reset_;
	/// the current time offset (or NOT_ASSIGNED if not yet assigned)
	double timeoffset_;
	/// remote computer time at the specified timeoffset_
	double remote_time_;
	/// round trip time (a.k.a. uncertainty) at the specficied timeoffset_
	double uncertainty_;
	/// mutex to protect the time offset
	std::mutex timeoffset_mut_;
	/// condition variable to indicate that an update for the time offset is available
	std::condition_variable timeoffset_upd_;


	// data used internally by the background thread
	/// the configuration object
	const api_config *cfg_;
	/// an IO service for async time operations
	asio::io_context time_io_;
	/// a buffer to hold inbound packet contents
	char recv_buffer_[16384]{0};
	/// the socket through which the time thread communicates
	udp::socket time_sock_;
	/// schedule the next time estimate
	asio::steady_timer next_estimate_;
	/// schedules result aggregation
	asio::steady_timer aggregate_results_;
	/// schedules the next packet transfer
	asio::steady_timer next_packet_;
	/// a dummy endpoint
	udp::endpoint remote_endpoint_;
	/// a vector of time estimates collected so far during the current exchange
	estimate_list estimates_;
	/// a vector of the local time and the remote time at a given estimate
	estimate_list estimate_times_;
	/// an id for the current wave of time packets
	int current_wave_id_{0};
};
} // namespace lsl

#endif
