#pragma once

// Builds the ordered gacha item list from the completed Fill() placement and stores it in
// the Context. Must be called after GeneratePlaythrough() and PareDownPlaythrough() succeed.
void BuildGachaList();
