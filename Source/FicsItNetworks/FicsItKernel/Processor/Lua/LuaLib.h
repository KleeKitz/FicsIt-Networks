#pragma once

#include "FGFactoryConnectionComponent.h"
#include "FGPowerCircuit.h"
#include "FGTrain.h"
#include "LuaInstance.h"
#include "Network/FINHookSubsystem.h"
#include "Delegates/DelegateSignatureImpl.inl"
#include "FicsItKernel/Network/SmartSignal.h"
#include "mod/hooking.h"

#include "LuaLib.generated.h"

class UFGFactoryConnectionComponent_Public;
FORCEINLINE FFINSignal SmartAsFINSig(FicsItKernel::Network::SmartSignal* sig) {
	return FFINSignal(std::shared_ptr<FicsItKernel::Network::SmartSignal>(sig));
}

namespace FicsItKernel {
	namespace Lua {
		/**
		* This function is used to manage the list pre defines lua library functions
		* which will get filled with registering closures static initialization.
		*/
		class LuaLib {
		public:
			/**
			 * The function type getting called when the LuaLib should get registered.
			 * The arguments should output the instance type, the instance type name
			 * and a set of LuaLibFunc/LuaLibClassFunc with name pairs.
			 */
			typedef std::function<void(UClass*&, std::string&, std::vector<std::pair<std::string, LuaLibFunc>>&, TSubclassOf<UFINHook>&)> ToRegisterFunc;
			typedef std::function<void(UClass*&, std::string&, std::vector<std::pair<std::string, LuaLibClassFunc>>&, TSubclassOf<UFINHook>&)> ToRegisterClassFunc;

		private:
			/**
			 * A set with to register functions which will get used to register
			 * when the library should get registered.
			 */
			std::vector<ToRegisterFunc> toRegister;
			std::vector<ToRegisterClassFunc> toRegisterClasses;
			
			LuaLib() = default;
		public:
			/**
			* Returns the instance of the LuaLib singleton.
			*
			* @return	instance of the LuaLib singleton.
			*/
			static LuaLib* get();

			/**
			 * Gets called when the module gets load to register all functions needed to get registered.
			 */
			void registerLib();

			/**
			 * Adds a new register function to the register functions.
			 *
			 * @param[in]	func	the to register func
			 */
			void registerRegFunc(const ToRegisterFunc& func);

			/**
			 * Adds a new register function to the register class functions.
			 *
			 * @param[in]	func	the to register class func
			 */
			void registerRegFunc(const ToRegisterClassFunc& func);
		};
	}
}

UCLASS()
class UFINTrainHook : public UFINHook {
	GENERATED_BODY()
			
private:
	UPROPERTY()
	UObject* Sender = nullptr;
			
public:	
	UFUNCTION()
	void SelfDrvingUpdate(bool enabled) {
		AFINHookSubsystem::GetHookSubsystem(this)->EmitSignal(Sender, FFINSignal(SmartAsFINSig(new FicsItKernel::Network::SmartSignal("SelfDrvingUpdate", {
			enabled
		}))));
	}
			
	void Register(UObject* sender) override {
		Sender = sender;
		Cast<AFGTrain>(sender)->mOnSelfDrivingChanged.AddDynamic(this, &UFINTrainHook::SelfDrvingUpdate);
	}
		
	void Unregister() override {
		Cast<AFGTrain>(Sender)->mOnSelfDrivingChanged.RemoveDynamic(this, &UFINTrainHook::SelfDrvingUpdate);
	}
};

UCLASS()
class UFINFactoryConnectorHook : public UFINHook {
	GENERATED_BODY()
			
private:
	UPROPERTY()
	UObject* Sender;
	
    static TSet<FWeakObjectPtr> Senders;
	static bool registered;

	static FCriticalSection MutexFactoryGrab;
	static TMap<TWeakObjectPtr<UFGFactoryConnectionComponent>, int8> FactoryGrabsRunning;

	static void LockFactoryGrab(UFGFactoryConnectionComponent* comp) {
		MutexFactoryGrab.Lock();
		++FactoryGrabsRunning.FindOrAdd(comp);
		MutexFactoryGrab.Unlock();
	}

	static bool UnlockFactoryGrab(UFGFactoryConnectionComponent* comp) {
		MutexFactoryGrab.Lock();
		int8* i = FactoryGrabsRunning.Find(comp);
		bool valid = false;
		if (i) {
			--*i;
			valid = (*i <= 0);
			if (valid) FactoryGrabsRunning.Remove(comp);
		}
		MutexFactoryGrab.Unlock();
		return valid;
	}

	static void DoFactoryGrab(UFGFactoryConnectionComponent* c, FInventoryItem& item) {
		AFINHookSubsystem::GetHookSubsystem(c)->EmitSignal(c, SmartAsFINSig(new FicsItKernel::Network::SmartSignal("ItemTransfer", {item})));
	}

	static void FactoryGrabHook(CallScope<bool(*)(UFGFactoryConnectionComponent*, FInventoryItem&, float&, TSubclassOf<UFGItemDescriptor>)>& scope, UFGFactoryConnectionComponent* c, FInventoryItem& item, float& offset, TSubclassOf<UFGItemDescriptor> type) {
		if (!Senders.Contains(c)) return;
		LockFactoryGrab(c);
		scope(c, item, offset, type);
		if (UnlockFactoryGrab(c) && scope.getResult()) {
			DoFactoryGrab(c, item);
		}
	}

	static void FactoryGrabInternalHook(CallScope<bool(*)(UFGFactoryConnectionComponent*, FInventoryItem&, TSubclassOf<UFGItemDescriptor>)>& scope, UFGFactoryConnectionComponent* c, FInventoryItem& item, TSubclassOf< UFGItemDescriptor > type) {
		if (!Senders.Contains(c)) return;
		LockFactoryGrab(c);
		scope(c, item, type);
		if (UnlockFactoryGrab(c) && scope.getResult()) {
			DoFactoryGrab(c, item);
		}
	}
			
public:		
	void Register(UObject* sender) override {
    	Senders.Add(Sender = sender);

		if (!registered) {
			registered = true;

			SUBSCRIBE_METHOD_MANUAL("?Factory_GrabOutput@UFGFactoryConnectionComponent@@QEAA_NAEAUFInventoryItem@@AEAMV?$TSubclassOf@VUFGItemDescriptor@@@@@Z", UFGFactoryConnectionComponent::Factory_GrabOutput, &FactoryGrabHook);
			SUBSCRIBE_METHOD(UFGFactoryConnectionComponent::Factory_Internal_GrabOutputInventory, &FactoryGrabInternalHook);
		}
    }
		
	void Unregister() override {
    	Senders.Remove(Sender);
    }
};

UCLASS()
class UFINPowerCircuitHook : public UFINHook {
	GENERATED_BODY()
			
private:
	UPROPERTY()
	UObject* Sender;
	
    static TSet<FWeakObjectPtr> Senders;
	static bool registered;

	static FCriticalSection Mutex;
	static TMap<TWeakObjectPtr<UFGFactoryConnectionComponent>, int8> FactoryGrabsRunning;

	static void TickCircuitHook_Decl(UFGPowerCircuit*, float);
	static void TickCircuitHook(CallScope<void(*)(UFGPowerCircuit*, float)>& scope, UFGPowerCircuit* circuit, float dt) {
		bool oldFused = circuit->IsFuseTriggered();
		scope(circuit, dt);
		bool fused = circuit->IsFuseTriggered();
		if (oldFused != fused) try {
			Mutex.Lock();
			FWeakObjectPtr* sender = Senders.Find(circuit);
			if (sender) {
				UObject* obj = sender->Get();
				AFINHookSubsystem::GetHookSubsystem(obj)->EmitSignal(obj, SmartAsFINSig(new FicsItKernel::Network::SmartSignal("PowerFuseChanged")));
			}
			Mutex.Unlock();
		} catch (...) {}
	}
			
public:		
	void Register(UObject* sender) override {
		Mutex.Lock();
    	Senders.Add(Sender = sender);

		if (!registered) {
			registered = true;
			
			SUBSCRIBE_METHOD_MANUAL("?TickCircuit@UFGPowerCircuit@@MEAAXM@Z", TickCircuitHook_Decl, &TickCircuitHook);
		}
		Mutex.Unlock();
    }
		
	void Unregister() override {
		Mutex.Lock();
    	Senders.Remove(Sender);
		Mutex.Unlock();
    }
};
