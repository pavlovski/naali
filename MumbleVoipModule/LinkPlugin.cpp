#include "StableHeaders.h"
#include "LinkPlugin.h"
#include "LinkedMem.h"

//! QString::ToStdWString() doesn't work with Visual Studio 2008
std::wstring QStringToStdWString(QString &s)
{
    std::string temp1 = s.toStdString();
    std::wstring temp2(temp1.size(), L'');
    std::copy(temp1.begin(), temp1.end(), temp2.begin());
    return temp2;
}

namespace MumbleVoip
{
	LinkPlugin::LinkPlugin(): linked_mem_(0)
	{
#ifdef WIN32
		HANDLE hMapObject = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, L"MumbleLink");
		if (hMapObject == NULL)
			return;

		linked_mem_ = (LinkedMem *) MapViewOfFile(hMapObject, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(LinkedMem));
		if (linked_mem_ == NULL) {
			CloseHandle(hMapObject);
			hMapObject = NULL;
			return;
		}
#else
		char memname[256];
		snprintf(memname, 256, "/MumbleLink.%d", getuid());

		int shmfd = shm_open(memname, O_RDWR, S_IRUSR | S_IWUSR);

		if (shmfd < 0) {
			return;
		}

		linked_mem_ = (LinkedMem *)(mmap(NULL, sizeof(struct LinkedMem), PROT_READ | PROT_WRITE, MAP_SHARED, shmfd,0));

		if (linked_mem_ == (void *)(-1)) {
			linked_mem_ = NULL;
			return;
		}
#endif
	}

	LinkPlugin::~LinkPlugin()
	{

	}

	void LinkPlugin::SendData()
	{
		if (! linked_mem_)
			return;

		if(linked_mem_->uiVersion != 2)
		{
            wcsncpy(linked_mem_->name, QStringToStdWString(avatar_name_).c_str(), 256);
            wcsncpy(linked_mem_->description, QStringToStdWString(description_).c_str(), 2048);
			linked_mem_->uiVersion = 2;
		}
		linked_mem_->uiTick++;

		linked_mem_->fAvatarFront[0] = avatar_position_.x;
		linked_mem_->fAvatarFront[1] = avatar_position_.y;
		linked_mem_->fAvatarFront[2] = avatar_position_.z;

		linked_mem_->fAvatarTop[0] = avatar_top_.x;
		linked_mem_->fAvatarTop[1] = avatar_top_.y;
		linked_mem_->fAvatarTop[2] = avatar_top_.z;

		linked_mem_->fAvatarPosition[0] = avatar_front_.x;
		linked_mem_->fAvatarPosition[1] = avatar_front_.y;
		linked_mem_->fAvatarPosition[2] = avatar_front_.z;

        linked_mem_->fCameraFront[0] = camera_front_.x;
		linked_mem_->fCameraFront[1] = camera_front_.y;
		linked_mem_->fCameraFront[2] = camera_front_.z;

		linked_mem_->fCameraTop[0] = camera_top_.x;
		linked_mem_->fCameraTop[1] = camera_top_.y;
		linked_mem_->fCameraTop[2] = camera_top_.z;

		linked_mem_->fCameraPosition[0] = camera_position_.x;
		linked_mem_->fCameraPosition[1] = camera_position_.y;
		linked_mem_->fCameraPosition[2] = camera_position_.z;

        // Identifier which uniquely identifies a certain player in a context (e.g. the ingame Name).
        wcsncpy(linked_mem_->identity, QStringToStdWString(avatar_id_).c_str(), 256);

        // Context should be equal for players which should be able to hear each other positional and
	    // differ for those who shouldn't (e.g. it could contain the server+port and team)
        int len = strlen(avatar_group_id_.toStdString().c_str());
        memcpy(linked_mem_->context, avatar_group_id_.toStdString().c_str(), len);
	    linked_mem_->context_len = len;
	}

	void LinkPlugin::SetAvatarName(const QString& name)
	{
        avatar_name_ = name;
        avatar_name_.truncate(255);
	}
	
	void LinkPlugin::SetAvatarIdentity(const QString& id)
	{
        avatar_id_ = id;
        avatar_id_.truncate(255);
	}

    void LinkPlugin::SetGroupId(const QString& id)
    {
        avatar_group_id_ = id;
        avatar_group_id_.truncate(255);
    }

    void LinkPlugin::SetDescription(const QString& description)
    {
        description_ = description;
        description_.truncate(2047);
    }
	
    void LinkPlugin::SetAvatarPosition(Vector3df position, Vector3df front, Vector3df top)
	{
        avatar_position_ = position;
        avatar_top_ = top;
        avatar_front_ = front;
	}
	
	void LinkPlugin::SetCameraPosition(Vector3df position, Vector3df front, Vector3df top)
	{
        camera_position_ = position;
        camera_top_ = top;
        camera_front_ = front;
	}

} // end of namespace: MumbleVoip