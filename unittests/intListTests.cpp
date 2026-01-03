#include <gtest/gtest.h>
#include "intlist.h"  // Include the iList interface header

// Fixture class for setting up common test elements
class ListTest : public ::testing::Test {
protected:
    intList* list;  // Pointer to the iList object

    void SetUp() override {
        list = iintList.Create();  // Assume there's a function to create an iList
    }

    void TearDown() override {
        iintList.Clear(list);  // Clear the list after each test
        iintList.Finalize(list);  // Destroy the list
    }
};

// Test creation of list
TEST_F(ListTest, Creation) {
    EXPECT_NE(list, nullptr);  // Ensure list is not null
}

// Test adding elements to the list
TEST_F(ListTest, AddElement) {
    int element = 42;
    EXPECT_EQ(iintList.Add(list, element), 0);  // Assume 0 indicates success

    // Check if the size of the list increased
    EXPECT_EQ(iintList.Size(list), 1);

    // Check if the first element is correct
    int* retrievedElement = iintList.GetElement(list, 0);
    ASSERT_NE(retrievedElement, nullptr);
    EXPECT_EQ(*retrievedElement, 42);
}

// Test removing an element from the list
TEST_F(ListTest, RemoveElement) {
    int element1 = 42, element2 = 84;
    iintList.Add(list, element1);
    iintList.Add(list, element2);

    EXPECT_EQ(iintList.RemoveRange(list, 0, 1), 0);  // Remove the element

    // Check if the list is empty
    EXPECT_EQ(iintList.Size(list), 1);
}

// Test clearing the list
TEST_F(ListTest, ClearList) {
    int element1 = 42, element2 = 84;
    iintList.Add(list, element1);
    iintList.Add(list, element2);

    iintList.Clear(list);  // Clear the list

    EXPECT_EQ(iintList.Size(list), 0);  // Check if list is empty
}

// Test retrieving an element from the list
TEST_F(ListTest, GetElement) {
    int element = 42;
    iintList.Add(list, element);

    int* retrievedElement = iintList.GetElement(list, 0);
    ASSERT_NE(retrievedElement, nullptr);  // Ensure element is not null
    EXPECT_EQ(*retrievedElement, 42);  // Ensure element is correct
}

// Test getting the size of the list
TEST_F(ListTest, ListSize) {
    EXPECT_EQ(iintList.Size(list), 0);  // Initially empty

    int element = 42;
    iintList.Add(list, element);

    EXPECT_EQ(iintList.Size(list), 1);  // One element added
}

// Test that list handles invalid index gracefully
TEST_F(ListTest, InvalidIndex) {
    int* retrievedElement = (int*)iintList.GetElement(list, 0);  // Empty list
    EXPECT_EQ(retrievedElement, nullptr);  // Should return null on empty list

    int element = 42;
    iintList.Add(list, element);

    retrievedElement = (int*)iintList.GetElement(list, 1);  // Out of bounds
    EXPECT_EQ(retrievedElement, nullptr);  // Should return null on out of bounds
}

