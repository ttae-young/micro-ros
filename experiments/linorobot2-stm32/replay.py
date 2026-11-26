"""Drive the on-board micro-ROS node from ROS 2 Humble and verify its replies."""
import argparse
import csv
import math
from pathlib import Path
import statistics
import time

import rclpy
from geometry_msgs.msg import Twist
from nav_msgs.msg import Odometry

WARMUP = 10
SAMPLES = 100
ENCODER_RPM = [(0, 0), (30, 30), (42, 66), (68, 40), (70, 70),
               (-32, -32), (-30, 30), (12, -18)]


def wait_until(node, predicate, deadline, error):
    while not predicate():
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError(error)
        rclpy.spin_once(node, timeout_sec=min(remaining, 0.1))


def run(node, writer):
    reply = {}

    def receive(msg):
        # Record receipt before validating the message or writing CSV.
        stamp = msg.header.stamp
        key = stamp.sec * 50 + stamp.nanosec // 20_000_000
        reply[key] = (msg, time.perf_counter_ns())

    node.create_subscription(Odometry, "/linorobot2_bench/odom", receive, 10)
    pub = node.create_publisher(Twist, "/linorobot2_bench/cmd_vel", 10)
    wait_until(node, lambda: pub.get_subscription_count() and node.count_publishers("/linorobot2_bench/odom"),
               time.monotonic() + 40, "Board endpoints not discovered")

    x = y = heading = 0.0
    measured = []
    writer.writerow(["round", "phase", "host_round_trip_us", "x_m", "y_m", "yaw_rad"])
    for i in range(1, WARMUP + SAMPLES + 1):
        command = Twist()
        command.linear.x = (0.15, 0.25, 0.30, 0.10, 0.35, -0.15, 0.0, 0.0)[(i - 1) % 8]
        command.angular.z = (0.0, 0.0, 0.6, -0.6, 0.0, 0.0, 0.9, -0.5)[(i - 1) % 8]
        start = time.perf_counter_ns()
        pub.publish(command)
        wait_until(node, lambda: i in reply, time.monotonic() + 12,
                   f"No Odometry response for round {i}")
        msg, received = reply.pop(i)
        if msg.header.frame_id != "odom" or msg.child_frame_id != "base_footprint":
            raise ValueError(f"Unexpected Odometry frames at round {i}")

        left, right = ENCODER_RPM[(i - 1) % len(ENCODER_RPM)]
        velocity = (left + right) * (0.065 * math.pi) / 120
        yaw_rate = (right - left) * (0.065 * math.pi) / (120 * 0.095)
        x += 0.02 * velocity * math.cos(heading)
        y += 0.02 * velocity * math.sin(heading)
        heading += 0.02 * yaw_rate
        position = msg.pose.pose.position
        yaw = 2 * math.atan2(msg.pose.pose.orientation.z, msg.pose.pose.orientation.w)
        pairs = ((position.x, x), (position.y, y), (yaw, heading),
                 (msg.twist.twist.linear.x, velocity), (msg.twist.twist.angular.z, yaw_rate))
        if any(not math.isclose(actual, expected, rel_tol=1e-5, abs_tol=2e-5) for actual, expected in pairs):
            raise ValueError(f"Odometry verification failed at round {i}: {pairs}")
        elapsed = (received - start) / 1_000
        phase = "warmup" if i <= WARMUP else "measured"
        writer.writerow([i, phase, f"{elapsed:.3f}", position.x, position.y, yaw])
        if i > WARMUP:
            measured.append(elapsed)
    return measured


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    rclpy.init()
    node = rclpy.create_node("linorobot2_replay")
    try:
        with args.output.open("x", newline="", encoding="utf-8") as output:
            elapsed = run(node, csv.writer(output))
        print(f"PASS verified={len(elapsed)} mean_host_round_trip_us={statistics.mean(elapsed):.3f}")
        print(args.output)
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    main()
