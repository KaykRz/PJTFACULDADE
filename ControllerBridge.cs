using Nefarius.ViGEm.Client;
using Nefarius.ViGEm.Client.Targets;
using Nefarius.ViGEm.Client.Targets.Xbox360;

namespace PSPLinkUsb;

internal sealed class ControllerBridge : IDisposable
{
    private const uint PspSelect = 0x00000001;
    private const uint PspStart = 0x00000008;
    private const uint PspUp = 0x00000010;
    private const uint PspRight = 0x00000020;
    private const uint PspDown = 0x00000040;
    private const uint PspLeft = 0x00000080;
    private const uint PspL = 0x00000100;
    private const uint PspR = 0x00000200;
    private const uint PspTriangle = 0x00001000;
    private const uint PspCircle = 0x00002000;
    private const uint PspCross = 0x00004000;
    private const uint PspSquare = 0x00008000;

    private readonly ViGEmClient _client;
    private readonly IXbox360Controller _controller;
    private readonly short[] _axisCurve = BuildAxisCurve();
    private readonly bool _invertY;
    private int _selectHeldFrames;
    private int _selectPulseFrames;
    private bool _selectHoldConsumed;

    public ControllerBridge(bool invertY)
    {
        _invertY = invertY;
        _client = new ViGEmClient();
        _controller = _client.CreateXbox360Controller();
        _controller.Connect();
    }

    public void Update(Protocol.ResponseHeader response)
    {
        uint buttons = response.Buttons;
        bool rightAnalog = (response.Flags & Protocol.RightAnalogFlag) != 0;
        bool l2 = (response.Flags & Protocol.L2Flag) != 0;
        bool r2 = (response.Flags & Protocol.R2Flag) != 0;
        bool selectPressed = (buttons & PspSelect) != 0;
        bool modifierActive = rightAnalog || l2 || r2;
        bool sendSelect;

        if (modifierActive)
        {
            _selectHoldConsumed = true;
            selectPressed = false;
        }

        if (selectPressed)
        {
            _selectHeldFrames++;
            if (_selectHeldFrames >= 10)
                _selectHoldConsumed = true;
        }
        else
        {
            if (_selectHeldFrames > 0 && _selectHeldFrames < 10 && !_selectHoldConsumed)
                _selectPulseFrames = 3;
            _selectHeldFrames = 0;
            _selectHoldConsumed = false;
        }

        sendSelect = _selectPulseFrames > 0;
        if (_selectPulseFrames > 0)
            _selectPulseFrames--;

        SetButton(Xbox360Button.A, (buttons & PspCross) != 0);
        SetButton(Xbox360Button.B, (buttons & PspCircle) != 0);
        SetButton(Xbox360Button.X, (buttons & PspSquare) != 0);
        SetButton(Xbox360Button.Y, (buttons & PspTriangle) != 0);
        SetButton(Xbox360Button.Back, sendSelect);
        SetButton(Xbox360Button.Start, (buttons & PspStart) != 0);
        SetButton(Xbox360Button.LeftShoulder, (buttons & PspL) != 0);
        SetButton(Xbox360Button.RightShoulder, (buttons & PspR) != 0);
        SetButton(Xbox360Button.Up, (buttons & PspUp) != 0);
        SetButton(Xbox360Button.Right, (buttons & PspRight) != 0);
        SetButton(Xbox360Button.Down, (buttons & PspDown) != 0);
        SetButton(Xbox360Button.Left, (buttons & PspLeft) != 0);

        byte leftTrigger = l2 ? byte.MaxValue : (byte)0;
        byte rightTrigger = r2 ? byte.MaxValue : (byte)0;
        _controller.SetSliderValue(Xbox360Slider.LeftTrigger, leftTrigger);
        _controller.SetSliderValue(Xbox360Slider.RightTrigger, rightTrigger);

        short x = Axis(response.AnalogX);
        short y = AxisY(response.AnalogY);
        if (rightAnalog)
        {
            _controller.SetAxisValue(Xbox360Axis.LeftThumbX, 0);
            _controller.SetAxisValue(Xbox360Axis.LeftThumbY, 0);
            _controller.SetAxisValue(Xbox360Axis.RightThumbX, x);
            _controller.SetAxisValue(Xbox360Axis.RightThumbY, y);
        }
        else
        {
            _controller.SetAxisValue(Xbox360Axis.LeftThumbX, x);
            _controller.SetAxisValue(Xbox360Axis.LeftThumbY, y);
            _controller.SetAxisValue(Xbox360Axis.RightThumbX, 0);
            _controller.SetAxisValue(Xbox360Axis.RightThumbY, 0);
        }

        _controller.SubmitReport();
    }

    private void SetButton(Xbox360Button button, bool pressed)
    {
        _controller.SetButtonState(button, pressed);
    }

    private short AxisY(byte value)
    {
        return NegateAxis(Axis(value));
    }

    private short Axis(byte value)
    {
        return _axisCurve[value];
    }

    private static short NegateAxis(short value)
    {
        return value == short.MinValue ? short.MaxValue : (short)-value;
    }

    private static short ComputeAxis(byte value)
    {
        int centered = value - 128;
        if (centered >= -14 && centered <= 14)
            return 0;

        int sign = centered < 0 ? -1 : 1;
        int magnitude = Math.Abs(centered);
        int normalized = (magnitude - 14) * 32767 / (127 - 14);
        if (normalized < 0)
            normalized = 0;
        if (normalized > 32767)
            normalized = 32767;

        return (short)(normalized * sign);
    }

    private static short[] BuildAxisCurve()
    {
        short[] curve = new short[256];
        for (int i = 0; i < curve.Length; i++)
            curve[i] = ComputeAxis((byte)i);
        curve[0] = short.MinValue;
        curve[255] = short.MaxValue;
        return curve;
    }

    public void Dispose()
    {
        _controller.Disconnect();
        _client.Dispose();
    }
}
