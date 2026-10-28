#include "display_skin.hpp"

namespace p4radio {
namespace {

class SquareSkin final : public DisplaySkin {
public:
    const char *name() const override { return "Square"; }

private:
    void build_controls() override
    {
        group_label("VOLUME", 278);
        add_button("-", 52, 314, 284, 112, Action::VolumeDown);
        add_button("+", 384, 314, 284, 112, Action::VolumeUp);
        group_label("STATION", 444);
        add_button("<", 52, 480, 284, 112, Action::PreviousStation);
        add_button(">", 384, 480, 284, 112, Action::NextStation);
        add_button("OFF", 52, 620, 284, 76, Action::Off);
        add_button("TUNE", 384, 620, 284, 76, Action::Tune);
    }

    // Shared widget positions preserve the original 720 x 720 layout.
    void arrange_widgets() override {}
};

} // namespace

DisplaySkin &square_display_skin()
{
    static SquareSkin skin;
    return skin;
}

} // namespace p4radio
