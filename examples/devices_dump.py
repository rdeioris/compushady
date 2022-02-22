import compushady

for device in compushady.get_discovered_devices():
    print('{0} dedicated: {1}MB shared: {2}MB {3} {4} {5}'.format(
        device.name, device.dedicated_video_memory // 1024 // 1024,
        device.shared_system_memory // 1024 // 1024,
        'hardware' if device.is_hardware else 'software',
        'discrete' if device.is_discrete else 'integrated' if device.is_hardware else '',
        '[best]' if device == compushady.get_best_device() else ''))