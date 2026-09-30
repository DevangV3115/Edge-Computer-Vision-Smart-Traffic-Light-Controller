from src.mqtt.publisher import build_signal_payload


def test_signal_payload():
    assert build_signal_payload(1, "GREEN", 20) == '{"lane":1,"signal":"GREEN","duration":20}'


def test_signal_is_normalized():
    assert build_signal_payload(2, "green", 15) == '{"lane":2,"signal":"GREEN","duration":15}'
