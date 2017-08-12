#include "BoundingBox.h"

bool BoundingBox::Intersects(BoundingBox *otherBox) {
    return ((left < otherBox->right) 
                && (right > otherBox->left) 
                &&        (bottom < otherBox->top) 
                && (top > otherBox->bottom));
}
