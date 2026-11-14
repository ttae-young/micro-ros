Write-Host 'Keep this window open. Flash/read RTT using 2_flash_and_rtt.ps1 in another window.'
& wsl.exe -d Ubuntu-22.04 -- bash -lc 'source /opt/ros/humble/setup.bash; source ~/microros_agent_ws/install/local_setup.bash; ros2 run micro_ros_agent micro_ros_agent serial --dev /tmp/ttyMICROROS -b 115200 -v4'
