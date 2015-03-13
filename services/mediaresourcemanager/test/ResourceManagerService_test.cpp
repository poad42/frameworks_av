/*
 * Copyright 2015 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

//#define LOG_NDEBUG 0
#define LOG_TAG "ResourceManagerService_test"
#include <utils/Log.h>

#include <gtest/gtest.h>

#include "ResourceManagerService.h"
#include <media/IResourceManagerService.h>
#include <media/MediaResource.h>
#include <media/MediaResourcePolicy.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/ProcessInfoInterface.h>

namespace android {

struct TestProcessInfo : public ProcessInfoInterface {
    TestProcessInfo() {}
    virtual ~TestProcessInfo() {}

    virtual bool getPriority(int pid, int *priority) {
        // For testing, use pid as priority.
        // Lower the value higher the priority.
        *priority = pid;
        return true;
    }

private:
    DISALLOW_EVIL_CONSTRUCTORS(TestProcessInfo);
};

struct TestClient : public BnResourceManagerClient {
    TestClient(sp<ResourceManagerService> service)
        : mReclaimed(false), mService(service) {}

    virtual bool reclaimResource() {
        sp<IResourceManagerClient> client(this);
        mService->removeResource((int64_t) client.get());
        mReclaimed = true;
        return true;
    }

    bool reclaimed() const {
        return mReclaimed;
    }

    void reset() {
        mReclaimed = false;
    }

protected:
    virtual ~TestClient() {}

private:
    bool mReclaimed;
    sp<ResourceManagerService> mService;
    DISALLOW_EVIL_CONSTRUCTORS(TestClient);
};

static const int kTestPid1 = 30;
static const int kTestPid2 = 20;

class ResourceManagerServiceTest : public ::testing::Test {
public:
    ResourceManagerServiceTest()
        : mService(new ResourceManagerService(new TestProcessInfo)),
          mTestClient1(new TestClient(mService)),
          mTestClient2(new TestClient(mService)),
          mTestClient3(new TestClient(mService)) {
    }

protected:
    static bool isEqualResources(const Vector<MediaResource> &resources1,
            const Vector<MediaResource> &resources2) {
        if (resources1.size() != resources2.size()) {
            return false;
        }
        for (size_t i = 0; i < resources1.size(); ++i) {
            if (resources1[i] != resources2[i]) {
                return false;
            }
        }
        return true;
    }

    static void expectEqResourceInfo(const ResourceInfo &info, sp<IResourceManagerClient> client,
            const Vector<MediaResource> &resources) {
        EXPECT_EQ(client, info.client);
        EXPECT_TRUE(isEqualResources(resources, info.resources));
    }

    void verifyClients(bool c1, bool c2, bool c3) {
        TestClient *client1 = static_cast<TestClient*>(mTestClient1.get());
        TestClient *client2 = static_cast<TestClient*>(mTestClient2.get());
        TestClient *client3 = static_cast<TestClient*>(mTestClient3.get());

        EXPECT_EQ(c1, client1->reclaimed());
        EXPECT_EQ(c2, client2->reclaimed());
        EXPECT_EQ(c3, client3->reclaimed());

        client1->reset();
        client2->reset();
        client3->reset();
    }

    // test set up
    // ---------------------------------------------------------------------------------
    //   pid                priority         client           type               number
    // ---------------------------------------------------------------------------------
    //   kTestPid1(30)      30               mTestClient1     secure codec       1
    //                                                        graphic memory     200
    //                                                        graphic memory     200
    // ---------------------------------------------------------------------------------
    //   kTestPid2(20)      20               mTestClient2     non-secure codec   1
    //                                                        graphic memory     300
    //                                       -------------------------------------------
    //                                       mTestClient3     secure codec       1
    //                                                        graphic memory     100
    // ---------------------------------------------------------------------------------
    void addResource() {
        // kTestPid1 mTestClient1
        Vector<MediaResource> resources1;
        resources1.push_back(MediaResource(String8(kResourceSecureCodec), 1));
        mService->addResource(kTestPid1, (int64_t) mTestClient1.get(), mTestClient1, resources1);
        resources1.push_back(MediaResource(String8(kResourceGraphicMemory), 200));
        Vector<MediaResource> resources11;
        resources11.push_back(MediaResource(String8(kResourceGraphicMemory), 200));
        mService->addResource(kTestPid1, (int64_t) mTestClient1.get(), mTestClient1, resources11);

        // kTestPid2 mTestClient2
        Vector<MediaResource> resources2;
        resources2.push_back(MediaResource(String8(kResourceNonSecureCodec), 1));
        resources2.push_back(MediaResource(String8(kResourceGraphicMemory), 300));
        mService->addResource(kTestPid2, (int64_t) mTestClient2.get(), mTestClient2, resources2);

        // kTestPid2 mTestClient3
        Vector<MediaResource> resources3;
        mService->addResource(kTestPid2, (int64_t) mTestClient3.get(), mTestClient3, resources3);
        resources3.push_back(MediaResource(String8(kResourceSecureCodec), 1));
        resources3.push_back(MediaResource(String8(kResourceGraphicMemory), 100));
        mService->addResource(kTestPid2, (int64_t) mTestClient3.get(), mTestClient3, resources3);

        const PidResourceInfosMap &map = mService->mMap;
        EXPECT_EQ(2u, map.size());
        ssize_t index1 = map.indexOfKey(kTestPid1);
        ASSERT_GE(index1, 0);
        const ResourceInfos &infos1 = map[index1];
        EXPECT_EQ(1u, infos1.size());
        expectEqResourceInfo(infos1[0], mTestClient1, resources1);

        ssize_t index2 = map.indexOfKey(kTestPid2);
        ASSERT_GE(index2, 0);
        const ResourceInfos &infos2 = map[index2];
        EXPECT_EQ(2u, infos2.size());
        expectEqResourceInfo(infos2[0], mTestClient2, resources2);
        expectEqResourceInfo(infos2[1], mTestClient3, resources3);
    }

    void testConfig() {
        EXPECT_TRUE(mService->mSupportsMultipleSecureCodecs);
        EXPECT_TRUE(mService->mSupportsSecureWithNonSecureCodec);

        Vector<MediaResourcePolicy> policies1;
        policies1.push_back(MediaResourcePolicy(String8(kPolicySupportsMultipleSecureCodecs), 1));
        policies1.push_back(
                MediaResourcePolicy(String8(kPolicySupportsSecureWithNonSecureCodec), 0));
        mService->config(policies1);
        EXPECT_TRUE(mService->mSupportsMultipleSecureCodecs);
        EXPECT_FALSE(mService->mSupportsSecureWithNonSecureCodec);

        Vector<MediaResourcePolicy> policies2;
        policies2.push_back(MediaResourcePolicy(String8(kPolicySupportsMultipleSecureCodecs), 0));
        policies2.push_back(
                MediaResourcePolicy(String8(kPolicySupportsSecureWithNonSecureCodec), 1));
        mService->config(policies2);
        EXPECT_FALSE(mService->mSupportsMultipleSecureCodecs);
        EXPECT_TRUE(mService->mSupportsSecureWithNonSecureCodec);
    }

    void testRemoveResource() {
        addResource();

        mService->removeResource((int64_t) mTestClient2.get());

        const PidResourceInfosMap &map = mService->mMap;
        EXPECT_EQ(2u, map.size());
        const ResourceInfos &infos1 = map.valueFor(kTestPid1);
        const ResourceInfos &infos2 = map.valueFor(kTestPid2);
        EXPECT_EQ(1u, infos1.size());
        EXPECT_EQ(1u, infos2.size());
        // mTestClient2 has been removed.
        EXPECT_EQ(mTestClient3, infos2[0].client);
    }

    void testGetAllClients() {
        addResource();

        String8 type = String8(kResourceSecureCodec);
        String8 unknowType = String8("unknowType");
        Vector<sp<IResourceManagerClient> > clients;
        int lowPriorityPid = 100;
        EXPECT_FALSE(mService->getAllClients_l(lowPriorityPid, type, &clients));
        int midPriorityPid = 25;
        // some higher priority process (e.g. kTestPid2) owns the resource, so getAllClients_l
        // will fail.
        EXPECT_FALSE(mService->getAllClients_l(midPriorityPid, type, &clients));
        int highPriorityPid = 10;
        EXPECT_TRUE(mService->getAllClients_l(highPriorityPid, unknowType, &clients));
        EXPECT_TRUE(mService->getAllClients_l(highPriorityPid, type, &clients));

        EXPECT_EQ(2u, clients.size());
        EXPECT_EQ(mTestClient3, clients[0]);
        EXPECT_EQ(mTestClient1, clients[1]);
    }

    void testReclaimResourceSecure() {
        Vector<MediaResource> resources;
        resources.push_back(MediaResource(String8(kResourceSecureCodec), 1));
        resources.push_back(MediaResource(String8(kResourceGraphicMemory), 150));

        // ### secure codec can't coexist and secure codec can coexist with non-secure codec ###
        {
            addResource();
            mService->mSupportsMultipleSecureCodecs = false;
            mService->mSupportsSecureWithNonSecureCodec = true;

            // priority too low
            EXPECT_FALSE(mService->reclaimResource(40, resources));
            EXPECT_FALSE(mService->reclaimResource(25, resources));

            // reclaim all secure codecs
            EXPECT_TRUE(mService->reclaimResource(10, resources));
            verifyClients(true, false, true);

            // call again should reclaim one largest graphic memory from lowest process
            EXPECT_TRUE(mService->reclaimResource(10, resources));
            verifyClients(false, true, false);

            // nothing left
            EXPECT_FALSE(mService->reclaimResource(10, resources));
        }

        // ### secure codecs can't coexist and secure codec can't coexist with non-secure codec ###
        {
            addResource();
            mService->mSupportsMultipleSecureCodecs = false;
            mService->mSupportsSecureWithNonSecureCodec = false;

            // priority too low
            EXPECT_FALSE(mService->reclaimResource(40, resources));
            EXPECT_FALSE(mService->reclaimResource(25, resources));

            // reclaim all secure and non-secure codecs
            EXPECT_TRUE(mService->reclaimResource(10, resources));
            verifyClients(true, true, true);

            // nothing left
            EXPECT_FALSE(mService->reclaimResource(10, resources));
        }


        // ### secure codecs can coexist but secure codec can't coexist with non-secure codec ###
        {
            addResource();
            mService->mSupportsMultipleSecureCodecs = true;
            mService->mSupportsSecureWithNonSecureCodec = false;

            // priority too low
            EXPECT_FALSE(mService->reclaimResource(40, resources));
            EXPECT_FALSE(mService->reclaimResource(25, resources));

            // reclaim all non-secure codecs
            EXPECT_TRUE(mService->reclaimResource(10, resources));
            verifyClients(false, true, false);

            // call again should reclaim one largest graphic memory from lowest process
            EXPECT_TRUE(mService->reclaimResource(10, resources));
            verifyClients(true, false, false);

            // call again should reclaim another largest graphic memory from lowest process
            EXPECT_TRUE(mService->reclaimResource(10, resources));
            verifyClients(false, false, true);

            // nothing left
            EXPECT_FALSE(mService->reclaimResource(10, resources));
        }

        // ### secure codecs can coexist and secure codec can coexist with non-secure codec ###
        {
            addResource();
            mService->mSupportsMultipleSecureCodecs = true;
            mService->mSupportsSecureWithNonSecureCodec = true;

            // priority too low
            EXPECT_FALSE(mService->reclaimResource(40, resources));

            EXPECT_TRUE(mService->reclaimResource(10, resources));
            // one largest graphic memory from lowest process got reclaimed
            verifyClients(true, false, false);

            // call again should reclaim another graphic memory from lowest process
            EXPECT_TRUE(mService->reclaimResource(10, resources));
            verifyClients(false, true, false);

            // call again should reclaim another graphic memory from lowest process
            EXPECT_TRUE(mService->reclaimResource(10, resources));
            verifyClients(false, false, true);

            // nothing left
            EXPECT_FALSE(mService->reclaimResource(10, resources));
        }

        // ### secure codecs can coexist and secure codec can coexist with non-secure codec ###
        {
            addResource();
            mService->mSupportsMultipleSecureCodecs = true;
            mService->mSupportsSecureWithNonSecureCodec = true;

            Vector<MediaResource> resources;
            resources.push_back(MediaResource(String8(kResourceSecureCodec), 1));

            EXPECT_TRUE(mService->reclaimResource(10, resources));
            // secure codec from lowest process got reclaimed
            verifyClients(true, false, false);

            // call again should reclaim another secure codec from lowest process
            EXPECT_TRUE(mService->reclaimResource(10, resources));
            verifyClients(false, false, true);

            // nothing left
            EXPECT_FALSE(mService->reclaimResource(10, resources));

            // clean up client 2 which still has non secure codec left
            mService->removeResource((int64_t) mTestClient2.get());
        }
    }

    void testReclaimResourceNonSecure() {
        Vector<MediaResource> resources;
        resources.push_back(MediaResource(String8(kResourceNonSecureCodec), 1));
        resources.push_back(MediaResource(String8(kResourceGraphicMemory), 150));

        // ### secure codec can't coexist with non-secure codec ###
        {
            addResource();
            mService->mSupportsSecureWithNonSecureCodec = false;

            // priority too low
            EXPECT_FALSE(mService->reclaimResource(40, resources));
            EXPECT_FALSE(mService->reclaimResource(25, resources));

            // reclaim all secure codecs
            EXPECT_TRUE(mService->reclaimResource(10, resources));
            verifyClients(true, false, true);

            // call again should reclaim one graphic memory from lowest process
            EXPECT_TRUE(mService->reclaimResource(10, resources));
            verifyClients(false, true, false);

            // nothing left
            EXPECT_FALSE(mService->reclaimResource(10, resources));
        }


        // ### secure codec can coexist with non-secure codec ###
        {
            addResource();
            mService->mSupportsSecureWithNonSecureCodec = true;

            // priority too low
            EXPECT_FALSE(mService->reclaimResource(40, resources));

            EXPECT_TRUE(mService->reclaimResource(10, resources));
            // one largest graphic memory from lowest process got reclaimed
            verifyClients(true, false, false);

            // call again should reclaim another graphic memory from lowest process
            EXPECT_TRUE(mService->reclaimResource(10, resources));
            verifyClients(false, true, false);

            // call again should reclaim another graphic memory from lowest process
            EXPECT_TRUE(mService->reclaimResource(10, resources));
            verifyClients(false, false, true);

            // nothing left
            EXPECT_FALSE(mService->reclaimResource(10, resources));
        }

        // ### secure codec can coexist with non-secure codec ###
        {
            addResource();
            mService->mSupportsSecureWithNonSecureCodec = true;

            Vector<MediaResource> resources;
            resources.push_back(MediaResource(String8(kResourceNonSecureCodec), 1));

            EXPECT_TRUE(mService->reclaimResource(10, resources));
            // one non secure codec from lowest process got reclaimed
            verifyClients(false, true, false);

            // nothing left
            EXPECT_FALSE(mService->reclaimResource(10, resources));

            // clean up client 1 and 3 which still have secure codec left
            mService->removeResource((int64_t) mTestClient1.get());
            mService->removeResource((int64_t) mTestClient3.get());
        }
    }

    void testGetLowestPriorityBiggestClient() {
        String8 type = String8(kResourceGraphicMemory);
        sp<IResourceManagerClient> client;
        EXPECT_FALSE(mService->getLowestPriorityBiggestClient_l(10, type, &client));

        addResource();

        EXPECT_FALSE(mService->getLowestPriorityBiggestClient_l(100, type, &client));
        EXPECT_TRUE(mService->getLowestPriorityBiggestClient_l(10, type, &client));

        // kTestPid1 is the lowest priority process with kResourceGraphicMemory.
        // mTestClient1 has the largest kResourceGraphicMemory within kTestPid1.
        EXPECT_EQ(mTestClient1, client);
    }

    void testGetLowestPriorityPid() {
        int pid;
        int priority;
        TestProcessInfo processInfo;

        String8 type = String8(kResourceGraphicMemory);
        EXPECT_FALSE(mService->getLowestPriorityPid_l(type, &pid, &priority));

        addResource();

        EXPECT_TRUE(mService->getLowestPriorityPid_l(type, &pid, &priority));
        EXPECT_EQ(kTestPid1, pid);
        int priority1;
        processInfo.getPriority(kTestPid1, &priority1);
        EXPECT_EQ(priority1, priority);

        type = String8(kResourceNonSecureCodec);
        EXPECT_TRUE(mService->getLowestPriorityPid_l(type, &pid, &priority));
        EXPECT_EQ(kTestPid2, pid);
        int priority2;
        processInfo.getPriority(kTestPid2, &priority2);
        EXPECT_EQ(priority2, priority);
    }

    void testGetBiggestClient() {
        String8 type = String8(kResourceGraphicMemory);
        sp<IResourceManagerClient> client;
        EXPECT_FALSE(mService->getBiggestClient_l(kTestPid2, type, &client));

        addResource();

        EXPECT_TRUE(mService->getBiggestClient_l(kTestPid2, type, &client));
        EXPECT_EQ(mTestClient2, client);
    }

    void testIsCallingPriorityHigher() {
        EXPECT_FALSE(mService->isCallingPriorityHigher_l(101, 100));
        EXPECT_FALSE(mService->isCallingPriorityHigher_l(100, 100));
        EXPECT_TRUE(mService->isCallingPriorityHigher_l(99, 100));
    }

    sp<ResourceManagerService> mService;
    sp<IResourceManagerClient> mTestClient1;
    sp<IResourceManagerClient> mTestClient2;
    sp<IResourceManagerClient> mTestClient3;
};

TEST_F(ResourceManagerServiceTest, config) {
    testConfig();
}

TEST_F(ResourceManagerServiceTest, addResource) {
    addResource();
}

TEST_F(ResourceManagerServiceTest, removeResource) {
    testRemoveResource();
}

TEST_F(ResourceManagerServiceTest, reclaimResource) {
    testReclaimResourceSecure();
    testReclaimResourceNonSecure();
}

TEST_F(ResourceManagerServiceTest, getAllClients_l) {
    testGetAllClients();
}

TEST_F(ResourceManagerServiceTest, getLowestPriorityBiggestClient_l) {
    testGetLowestPriorityBiggestClient();
}

TEST_F(ResourceManagerServiceTest, getLowestPriorityPid_l) {
    testGetLowestPriorityPid();
}

TEST_F(ResourceManagerServiceTest, getBiggestClient_l) {
    testGetBiggestClient();
}

TEST_F(ResourceManagerServiceTest, isCallingPriorityHigher_l) {
    testIsCallingPriorityHigher();
}

} // namespace android
